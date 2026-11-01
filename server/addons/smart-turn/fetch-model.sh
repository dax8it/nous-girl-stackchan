#!/usr/bin/env bash
# Download Pipecat smart-turn v3.2 (CPU, int8, ~8.7 MB, BSD-2-Clause) into <server dir>/data/smart-turn/
# Usage: ./fetch-model.sh /path/to/xiaozhi-server
set -euo pipefail
SERVER="${1:?usage: fetch-model.sh <xiaozhi-server dir (the one with data/)>}"
SHA=2bb026316b14a660486a75b1733cd3fbab8c2fd0314dc9af7be49f8cca967e4f
DIR="$SERVER/data/smart-turn"; F="$DIR/smart-turn-v3.2-cpu.onnx"
mkdir -p "$DIR"
[ -f "$F" ] || curl -fL -o "$F" https://huggingface.co/pipecat-ai/smart-turn-v3/resolve/main/smart-turn-v3.2-cpu.onnx
GOT=$( (command -v sha256sum >/dev/null && sha256sum "$F" || shasum -a 256 "$F") | cut -d' ' -f1)
[ "$GOT" = "$SHA" ] && echo "OK $F" || { echo "checksum mismatch for $F"; exit 1; }
