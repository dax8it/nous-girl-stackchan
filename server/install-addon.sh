#!/usr/bin/env bash
# install-addon.sh: install one Nous-Girl add-on into a RUNNING xiaozhi-esp32-server container
# (and/or a folder for Hermes MCP files). It copies files with `docker cp` and runs
# `docker restart`. It never recreates, rebuilds or removes the container.
#
#   ./install-addon.sh list
#   ./install-addon.sh <addon> status        read only: which files are there, same or different
#   ./install-addon.sh <addon> dry-run       shows what apply would do, changes nothing
#   ./install-addon.sh <addon> apply         backs up, copies, checks, restarts, rolls back on failure
#   ./install-addon.sh <addon> rollback [TS] puts the backup back (newest one if TS is left out)
#
# Settings (environment variables, all optional):
#   NG_CONTAINER   container name                 (default: xiaozhi-esp32-server)
#   NG_SERVER_ROOT server folder in the container (default: /opt/xiaozhi-esp32-server)
#   NG_ADDONS_DIR  where Hermes-side files go     (default: ~/.nous-girl/hermes)
#   NG_BACKUP_DIR  where backups go               (default: ~/nous-girl-backups)
#   NG_OTA_PORT    host port of the server's HTTP/OTA endpoint, for the health check (default: 8003)
#   NG_HEALTH_URL  full health check URL          (default: http://127.0.0.1:$NG_OTA_PORT/xiaozhi/ota/)
#
# Each add-on has an addon.conf. One entry per line:
#   server <file in the add-on folder> <path inside NG_SERVER_ROOT>
#   hermes <file in the add-on folder> <path inside NG_ADDONS_DIR>
#   note   <text shown after apply>
# MIT License. See ../LICENSE.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CONTAINER="${NG_CONTAINER:-xiaozhi-esp32-server}"
ROOT="${NG_SERVER_ROOT:-/opt/xiaozhi-esp32-server}"
HDIR="${NG_ADDONS_DIR:-$HOME/.nous-girl/hermes}"
BACKUPS="${NG_BACKUP_DIR:-$HOME/nous-girl-backups}"
HEALTH="${NG_HEALTH_URL:-http://127.0.0.1:${NG_OTA_PORT:-8003}/xiaozhi/ota/}"

die() { echo "ERROR: $*" >&2; exit 1; }
say() { echo "  $*"; }

sha() {  # sha256 of a local file
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
csha() {  # sha256 of a file in the container, empty if missing
  docker exec "$CONTAINER" sh -c "test -f '$1' && sha256sum '$1' | cut -d' ' -f1" 2>/dev/null || true
}

usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

[ $# -ge 1 ] || usage 1
if [ "$1" = "list" ]; then
  for d in "$HERE"/addons/*/; do n="$(basename "$d")"; printf "  %-16s %s\n" "$n" "$(sed -n 's/^# *summary: *//p' "$d/addon.conf" 2>/dev/null | head -1)"; done
  exit 0
fi
case "$1" in -h|--help) usage 0 ;; esac
ADDON="$1"; CMD="${2:-status}"; TS_ARG="${3:-}"
DIR="$HERE/addons/$ADDON"; CONF="$DIR/addon.conf"
[ -f "$CONF" ] || die "no add-on called '$ADDON' (try: $0 list)"

# Read entries into a temp file: kind<TAB>src<TAB>dest
ENTRIES="$(mktemp)"; NOTES="$(mktemp)"; trap 'rm -f "$ENTRIES" "$NOTES"' EXIT
while IFS= read -r line || [ -n "$line" ]; do
  case "$line" in ''|'#'*) continue ;; esac
  kind="${line%% *}"; rest="${line#* }"
  case "$kind" in
    note) echo "$rest" >> "$NOTES" ;;
    server|hermes)
      src="${rest%% *}"; dest="${rest#* }"
      [ -f "$DIR/$src" ] || die "addon.conf lists $src but it is not in $DIR"
      case "$dest" in /*|*..*) die "destination must be a relative path without '..': $dest" ;; esac
      printf '%s\t%s\t%s\n' "$kind" "$src" "$dest" >> "$ENTRIES" ;;
    *) die "unknown line in addon.conf: $line" ;;
  esac
done < "$CONF"
HAS_SERVER=0; grep -q '^server' "$ENTRIES" && HAS_SERVER=1

need_container() {
  command -v docker >/dev/null 2>&1 || die "docker not found"
  [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null || true)" = "true" ] \
    || die "container '$CONTAINER' is not running (set NG_CONTAINER to your container name)"
}

healthy() {  # wait up to ~90 s for the container and its HTTP port
  i=0
  while [ $i -lt 45 ]; do
    if [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null || true)" = "true" ]; then
      code="$(curl -s -o /dev/null -m 3 -w '%{http_code}' "$HEALTH" 2>/dev/null || true)"
      case "$code" in 2*|3*|4*) say "health check OK (HTTP $code from the OTA endpoint)"; return 0 ;; esac
    fi
    sleep 2; i=$((i + 1))
  done
  return 1
}

do_status() {
  [ $HAS_SERVER = 1 ] && need_container
  while IFS="$(printf '\t')" read -r kind src dest; do
    want="$(sha "$DIR/$src")"
    if [ "$kind" = server ]; then have="$(csha "$ROOT/$dest")"; where="container:$dest"
    else have=""; [ -f "$HDIR/$dest" ] && have="$(sha "$HDIR/$dest")"; where="hermes:$dest"; fi
    if [ -z "$have" ]; then st="missing"; elif [ "$have" = "$want" ]; then st="installed"; else st="different"; fi
    printf "  %-10s %s\n" "$st" "$where"
  done < "$ENTRIES"
  [ -s "$ENTRIES" ] || say "this add-on has no files to copy; see its README.md"
  ls -1 "$BACKUPS/$ADDON" 2>/dev/null | sed 's/^/  backup: /' || true
}

do_dry() {
  [ $HAS_SERVER = 1 ] && need_container
  echo "Would back up to $BACKUPS/$ADDON/<timestamp>/, then copy:"
  while IFS="$(printf '\t')" read -r kind src dest; do
    if [ "$kind" = server ]; then say "docker cp $src -> $CONTAINER:$ROOT/$dest"; else say "cp $src -> $HDIR/$dest"; fi
  done < "$ENTRIES"
  [ $HAS_SERVER = 1 ] && say "then: python syntax check in the container, docker restart $CONTAINER, health check $HEALTH"
  if [ -s "$NOTES" ]; then echo "Then do these by hand:"; sed 's/^/  - /' "$NOTES"; fi
  echo "Nothing was changed."
}

restore() {  # restore backup dir $1
  B="$1"
  while IFS="$(printf '\t')" read -r kind src dest; do
    if [ "$kind" = server ]; then
      if [ -f "$B/server/$dest" ]; then docker cp "$B/server/$dest" "$CONTAINER:$ROOT/$dest"
      elif grep -qxF "$dest" "$B/absent-server.txt" 2>/dev/null; then docker exec "$CONTAINER" rm -f "$ROOT/$dest"; fi
    else
      if [ -f "$B/hermes/$dest" ]; then cp -p "$B/hermes/$dest" "$HDIR/$dest"
      elif grep -qxF "$dest" "$B/absent-hermes.txt" 2>/dev/null; then rm -f "$HDIR/$dest"; fi
    fi
  done < "$ENTRIES"
}

do_apply() {
  [ $HAS_SERVER = 1 ] && need_container
  TS="$(date +%Y%m%d-%H%M%S)"; B="$BACKUPS/$ADDON/$TS"
  mkdir -p "$B/server" "$B/hermes"; : > "$B/absent-server.txt"; : > "$B/absent-hermes.txt"
  echo "Backing up to $B"
  while IFS="$(printf '\t')" read -r kind src dest; do
    if [ "$kind" = server ]; then
      if [ -n "$(csha "$ROOT/$dest")" ]; then mkdir -p "$B/server/$(dirname "$dest")"; docker cp "$CONTAINER:$ROOT/$dest" "$B/server/$dest"
      else echo "$dest" >> "$B/absent-server.txt"; fi
    else
      if [ -f "$HDIR/$dest" ]; then mkdir -p "$B/hermes/$(dirname "$dest")"; cp -p "$HDIR/$dest" "$B/hermes/$dest"
      else echo "$dest" >> "$B/absent-hermes.txt"; fi
    fi
  done < "$ENTRIES"
  echo "Copying"
  while IFS="$(printf '\t')" read -r kind src dest; do
    if [ "$kind" = server ]; then
      docker exec "$CONTAINER" mkdir -p "$ROOT/$(dirname "$dest")"
      docker cp "$DIR/$src" "$CONTAINER:$ROOT/$dest"; say "$dest"
    else
      mkdir -p "$HDIR/$(dirname "$dest")"; cp "$DIR/$src" "$HDIR/$dest"; say "hermes: $HDIR/$dest"
    fi
  done < "$ENTRIES"
  if [ $HAS_SERVER = 1 ]; then
    echo "Checking Python syntax in the container"
    bad=0
    while IFS="$(printf '\t')" read -r kind src dest; do
      [ "$kind" = server ] || continue
      case "$dest" in *.py) docker exec "$CONTAINER" python -c 'import ast, sys; ast.parse(open(sys.argv[1], encoding="utf-8").read(), sys.argv[1])' "$ROOT/$dest" || { echo "  syntax error in $dest"; bad=1; } ;; esac
    done < "$ENTRIES"
    if [ $bad = 1 ]; then echo "Rolling back"; restore "$B"; die "nothing restarted; files put back"; fi
    echo "Restarting $CONTAINER (docker restart, the container is kept)"
    docker restart "$CONTAINER" >/dev/null
    if ! healthy; then
      echo "Server did not come back. Rolling back and restarting again."
      restore "$B"; docker restart "$CONTAINER" >/dev/null; healthy || true
      die "apply failed; backup restored. Check: docker logs --tail 80 $CONTAINER"
    fi
  fi
  echo "Done. Backup: $TS   (undo: $0 $ADDON rollback $TS)"
  if [ -s "$NOTES" ]; then echo "Next, by hand:"; sed 's/^/  - /' "$NOTES"; fi
  return 0
}

do_rollback() {
  [ $HAS_SERVER = 1 ] && need_container
  TS="$TS_ARG"; [ -n "$TS" ] || TS="$(ls -1 "$BACKUPS/$ADDON" 2>/dev/null | sort | tail -1)"
  [ -n "$TS" ] && [ -d "$BACKUPS/$ADDON/$TS" ] || die "no backup found in $BACKUPS/$ADDON"
  echo "Restoring $BACKUPS/$ADDON/$TS"
  restore "$BACKUPS/$ADDON/$TS"
  if [ $HAS_SERVER = 1 ]; then docker restart "$CONTAINER" >/dev/null; healthy || die "server did not come back; check docker logs $CONTAINER"; fi
  echo "Rolled back. Undo any config.yaml or SOUL.md lines you added by hand for this add-on."
}

case "$CMD" in
  status) do_status ;;
  dry-run) do_dry ;;
  apply) do_apply ;;
  rollback) do_rollback ;;
  *) usage 1 ;;
esac
