#!/usr/bin/env bash
# Turn smart-turn on or off in data/.config.yaml, then docker restart (never recreate).
# Usage: ./switch-vad.sh on|off|status <xiaozhi-server dir>
# Env: NG_CONTAINER (default xiaozhi-esp32-server), NG_BACKUP_DIR (default ~/nous-girl-backups)
set -euo pipefail
CMD="${1:?usage: switch-vad.sh on|off|status <server dir>}"; SERVER="${2:?server dir}"
C="${NG_CONTAINER:-xiaozhi-esp32-server}"; CFG="$SERVER/data/.config.yaml"
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -f "$CFG" ] || { echo "no $CFG"; exit 1; }
sedi() { if sed --version >/dev/null 2>&1; then sed -i "$@"; else sed -i '' "$@"; fi; }
backup() { local b="${NG_BACKUP_DIR:-$HOME/nous-girl-backups}/smart-turn/$(date +%Y%m%d-%H%M%S)-$$"; mkdir -p "$b"; cp -p "$CFG" "$b/.config.yaml"; echo "backup: $b/.config.yaml"; }
case "$CMD" in
  status) grep -n "^  VAD: " "$CFG"; grep -c "SileroSmartTurn:" "$CFG" | sed 's/^/SileroSmartTurn blocks: /'; exit 0 ;;
  on)
    [ -f "$SERVER/data/smart-turn/smart-turn-v3.2-cpu.onnx" ] || { echo "run fetch-model.sh first"; exit 1; }
    backup
    if ! grep -q "^  SileroSmartTurn:" "$CFG"; then
      sed -n '/^  SileroSmartTurn:/,$p' "$HERE/config-snippet.yaml" > "$CFG.blk"
      awk 'FNR==NR{b=b $0 "\n"; next} {print} /^VAD:[[:space:]]*$/{printf "%s", b}' "$CFG.blk" "$CFG" > "$CFG.tmp" \
        && cat "$CFG.tmp" > "$CFG"; rm -f "$CFG.tmp" "$CFG.blk"
    fi
    sedi 's/^  VAD: .*/  VAD: SileroSmartTurn/' "$CFG" ;;
  off) backup; sedi 's/^  VAD: .*/  VAD: SileroVAD/' "$CFG" ;;
  *) echo "on|off|status"; exit 1 ;;
esac
grep -n "^  VAD: " "$CFG"
docker restart "$C" >/dev/null && echo "restarted $C"
