# multilingual: five languages, picked per sentence

## What it does

Her voice speaks English, Spanish, French, German and Portuguese. The new TTS provider guesses the language of every sentence and sends it to that language's Phonon server, so a reply that switches language mid-way still gets the right accent. Short sentences ("Wow!") keep the previous language. If a language server fails it falls back to English, then to an optional English 31M server. The base prompt tells the model to answer in the child's language (default English) and never in Chinese.

## Requirements

- The voice server, stock or [patched](../patched-server/). The provider only needs its own file.
- Five [ptts-openai-server](https://github.com/gradium-ai/xn-ptts/blob/main/ptts-openai-server/README.md) instances, one per language, reachable from the voice server container.
- The **phonon-40m-multilingual** model: [huggingface.co/gradium/phonon-40m-multilingual](https://huggingface.co/gradium/phonon-40m-multilingual). It is **gated** and licensed **CC BY-NC 4.0 (non-commercial only;** commercial use: ask Gradium). Accept the terms on that page and use your own Hugging Face token (`HF_TOKEN`). The runtime is MIT OR Apache-2.0. The English-only [phonon-31m-en](https://gradium.ai/phonon) is Apache 2.0. **No model weights or voice files are in this repo.**
- RAM: each server loads its own copy of the 74.6 MB q8 model.

## Install

1. Start the Phonon servers, either with Docker:
   ```
   export HF_TOKEN=...        # your own token, never commit it
   docker compose -f multilingual/tools/phonon-compose.example.yml up -d
   ```
   or natively with [tools/start-phonon-ml.sh](./tools/start-phonon-ml.sh) (`start-phonon-ml.sh en 8661`, `es 8681`, `fr 8682`, `de 8683`, `pt 8684`).
2. Check one: `curl -s http://127.0.0.1:8661/v1/audio/speech -H "Content-Type: application/json" -d '{"model":"tts-1","input":"Hello.","voice":"default"}' -o hello.wav`
3. Copy the provider and prompt:
   ```
   ./install-addon.sh multilingual apply
   ```
4. Merge [config-snippet.yaml](./config-snippet.yaml) into `data/.config.yaml` (keep your old TTS block for rollback) and `docker restart <container>`.

**Reaching the Phonon servers from Docker:** on macOS and Windows (Docker Desktop) `host.docker.internal` works. On Linux, add `extra_hosts: ["host.docker.internal:host-gateway"]` and make sure the servers listen on an address the container can reach, or put the Phonon services in the same compose file as the voice server and use their service names (`http://phonon-en:8880/v1/audio/speech`).

## Verify

- `docker logs <container> | grep "PhononML ready"` shows `langs=['de', 'en', 'es', 'fr', 'pt']`.
- Say "speak Spanish": she answers in Spanish with a Spanish voice. "Back to English" switches back.

## Rollback

```
./install-addon.sh multilingual rollback
```

Set `selected_module.TTS` back to your old TTS, remove the `prompt_template` line, restart the container. Stop the Phonon servers if you don't need them.
