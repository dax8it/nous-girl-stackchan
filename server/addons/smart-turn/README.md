# smart-turn: she knows when you are done talking

## What it does

Stock xiaozhi ends your turn after a fixed stretch of silence (Silero VAD, `min_silence_duration_ms`). Too short and she cuts you off when you pause to think ("I want a song about... um..."). Too long and every answer feels slow.

This add-on keeps Silero for "is someone talking" and adds [Pipecat smart-turn v3.2](https://github.com/pipecat-ai/smart-turn), a small model that listens to *how* you sound at the end (intonation, trailing off) and says "done" or "still going".

- When Silero hears 0.3 s of silence, smart-turn scores the last 8 s of your turn (about 0.1 s on a CPU, in a worker thread).
- Sounds finished: she answers right away (about 0.5 s after you stop, instead of about 0.85 s with SileroVAD at 800 ms).
- Sounds unfinished: she keeps listening and asks again every 0.3 s. After 2.5 s of silence she answers anyway.
- Start talking again: the pending check is dropped.
- Any smart-turn error: she falls back to plain Silero behaviour. No audio is saved; only the score and timing are logged.

No firmware change. It works with any firmware build. It does not add barge-in (that needs echo cancellation on the device).

## Requirements

- The voice server image already has `onnxruntime` and `numpy` (stock xiaozhi-esp32-server does). `transformers` is **not** needed: the Whisper log-mel features are computed in numpy and match `WhisperFeatureExtractor(chunk_length=8)` to within 1e-7.
- The model, not in this repo: [`smart-turn-v3.2-cpu.onnx`](https://huggingface.co/pipecat-ai/smart-turn-v3) (8.7 MB, BSD-2-Clause, Daily / Pipecat). `fetch-model.sh` downloads it into `data/smart-turn/` and checks its SHA-256. `data/` is a volume in [docker-compose.example.yml](../../docker-compose.example.yml), so the model survives a container recreate.

## Install

```
cd server
./addons/smart-turn/fetch-model.sh /path/to/xiaozhi-server
NG_CONTAINER=<your container> ./install-addon.sh smart-turn apply      # docker cp the provider, syntax check, restart
NG_CONTAINER=<your container> ./addons/smart-turn/switch-vad.sh on /path/to/xiaozhi-server
```

`switch-vad.sh on` backs up `data/.config.yaml` to `~/nous-girl-backups/smart-turn/<timestamp>/`, adds the `SileroSmartTurn` block from [config-snippet.yaml](./config-snippet.yaml) (once), sets `selected_module.VAD: SileroSmartTurn` and runs `docker restart`. Nothing is recreated.

## Verify

```
docker logs --since 2m <container> 2>&1 | grep SmartTurn
```

You should see `SmartTurn ready model=smart-turn-v3.2-cpu.onnx ...` after the restart, then one line per check while you talk:

```
SmartTurn prob=0.974 COMPLETE silence=420ms audio=1.7s latency=119ms
SmartTurn prob=0.005 wait silence=420ms audio=2.1s latency=119ms
SmartTurn force stop silence=2522ms last_prob=0.007
```

Offline self test (no robot needed; put a few 16 kHz mono wav clips in `data/smart-turn/selftest/`, on a Mac `say -o x.wav --data-format=LEI16@16000 "Can you tell me about the"`):

```
docker exec -i -e PYTHONDONTWRITEBYTECODE=1 -w /opt/xiaozhi-esp32-server <container> python - < addons/smart-turn/tests/selftest.py
```

## Tuning

| Setting | Default | Effect |
|---|---|---|
| `min_silence_duration_ms` | 300 | silence before the first check. Lower = faster when you are done |
| `smart_turn_threshold` | 0.5 | higher = she waits more often |
| `smart_turn_recheck_ms` | 300 | how often she re-checks while you pause |
| `smart_turn_max_wait_ms` | 2500 | longest pause before she answers anyway |
| `smart_turn_threads` | 1 | ONNX threads (2 or 4 was not faster in tests) |

Noisy rooms: background talk keeps Silero "hearing voice", which delays every turn, with or without smart-turn.

## Roll back

```
NG_CONTAINER=<your container> ./addons/smart-turn/switch-vad.sh off /path/to/xiaozhi-server   # back to SileroVAD + restart
NG_CONTAINER=<your container> ./install-addon.sh smart-turn rollback                         # optional: remove the provider file
```

Or restore the `.config.yaml` backup and `docker restart`. The provider file is harmless when not selected.

## Licenses

Our code is MIT. The model is BSD-2-Clause, (c) Daily, from [pipecat-ai/smart-turn](https://github.com/pipecat-ai/smart-turn); it is downloaded, not shipped. The inference steps follow that repo's `inference.py` (last 8 s, zero-padded on the left, normalized, Whisper log-mel, sigmoid output).
