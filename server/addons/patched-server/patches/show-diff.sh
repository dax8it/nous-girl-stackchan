#!/usr/bin/env bash
# show-diff.sh: print the full diff between upstream xiaozhi-esp32-server at the base commit
# and the modified upstream files in ../files. Read only.
#
#   git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
#   ./show-diff.sh /path/to/xiaozhi-esp32-server > modified-upstream-files.diff
#
# MIT License. See ../../../../LICENSE.
set -euo pipefail
UP="${1:?usage: $0 /path/to/xiaozhi-esp32-server-clone}"
BASE=4745186cead1b0f07ff7f8b83a79e499795b88eb
FILES="$(cd "$(dirname "$0")/../files" && pwd)"
MODIFIED="core/api/ota_handler.py core/api/vision_handler.py core/connection.py
core/handle/intentHandler.py core/handle/sendAudioHandle.py core/http_server.py
core/providers/tts/gpt_sovits_v2.py core/providers/tts/openai.py core/utils/dialogue.py
core/websocket_server.py plugins_func/functions/play_music.py"
for f in $MODIFIED; do
  git -C "$UP" show "$BASE:main/xiaozhi-server/$f" \
    | diff -u --label "a/main/xiaozhi-server/$f" --label "b/main/xiaozhi-server/$f" - "$FILES/$f" || true
done
