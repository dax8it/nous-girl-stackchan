#!/usr/bin/env bash
# Start one Phonon 40M multilingual server for one language, without Docker.
# Usage: start-phonon-ml.sh <en|es|fr|de|pt> <port>
# Needs the ptts-openai-server binary (build it from https://github.com/gradium-ai/xn-ptts,
# see its ptts-openai-server/README.md) and, because the 40M models are gated, HF_TOKEN set
# after you accept the model terms on Hugging Face. Binds 127.0.0.1 only (the server has no auth).
# Env: PTTS_BIN (default: ptts-openai-server on PATH), PHONON_ML_MODEL, PHONON_VOICE_DIR (optional).
set -euo pipefail
LANG_CODE="${1:?language: en es fr de pt}"; PORT="${2:?port}"
export RAYON_NUM_THREADS="${RAYON_NUM_THREADS:-2}"
VOICE_ARGS=()
[ -n "${PHONON_VOICE_DIR:-}" ] && VOICE_ARGS=(--voice-dir "$PHONON_VOICE_DIR")
exec "${PTTS_BIN:-ptts-openai-server}" --addr "127.0.0.1:$PORT" --lang "$LANG_CODE" --quant q8 \
  --model "${PHONON_ML_MODEL:-hf://gradium/phonon-40m-multilingual}" "${VOICE_ARGS[@]+"${VOICE_ARGS[@]}"}" \
  --ignore-speed --max-concurrent-requests 2
