# local-stt-mlx: speech-to-text on your Mac (optional)

## What it does

A small OpenAI-compatible speech-to-text server (`POST /v1/audio/transcriptions`, `GET /health`) that runs on an Apple Silicon Mac with [MLX](https://github.com/ml-explore/mlx). The voice server sends her what it heard and gets text back, without a cloud service. It also:

- fixes her name when it is misheard ("news girl", "noose girl" become "Nous-Girl"), only for English or unclear text, `STT_NAME_FIX=0` turns it off;
- drops the phrases Whisper invents on silence ("thanks for watching") when they are the whole transcript.

Audio goes to a temp file, is transcribed and deleted. Only timings and text length are logged.

## Requirements

- An Apple Silicon Mac (MLX does not run on Intel Macs, Windows or Linux). On other computers keep the voice server's built-in ASR.
- Python 3.11 or 3.12 and `uv` (or pip).
- One model, downloaded on first start (not in this repo; check each model card for its license):
  - `STT_MODEL=parakeet` (default): [mlx-community/parakeet-tdt-0.6b-v3](https://huggingface.co/mlx-community/parakeet-tdt-0.6b-v3) via [parakeet-mlx](https://pypi.org/project/parakeet-mlx/)
  - `STT_MODEL=whisper`: [mlx-community/whisper-large-v3-turbo-asr-fp16](https://huggingface.co/mlx-community/whisper-large-v3-turbo-asr-fp16) via [mlx-audio](https://pypi.org/project/mlx-audio/)

## Install

```
uv run --python 3.12 --with fastapi --with uvicorn --with python-multipart --with numpy \
  --with parakeet-mlx python local-stt-mlx/stt_server.py
```

(For Whisper: `STT_MODEL=whisper` and `--with mlx-audio` instead of `--with parakeet-mlx`.) It listens on `127.0.0.1:8662`; change with `STT_HOST` / `STT_PORT`. Then add an ASR block to `data/.config.yaml` that uses the upstream OpenAI-compatible provider:

```yaml
selected_module:
  ASR: LocalMLXASR
ASR:
  LocalMLXASR:
    type: openai
    api_key: local                       # ignored by this server
    base_url: http://host.docker.internal:8662/v1/audio/transcriptions
    model_name: parakeet
    output_dir: tmp/
```

and `docker restart <container>`. To keep it running, start it from a launchd agent or a login item.

## Verify

`curl -s http://127.0.0.1:8662/health` shows the model and its load time. Talk to her: the server log shows `ASR` text from your words.

## Rollback

Set `selected_module.ASR` back to your old ASR, restart the container, stop `stt_server.py`.
