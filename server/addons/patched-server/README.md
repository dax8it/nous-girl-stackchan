# patched-server: the core code

The changes to [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) that the other server add-ons build on. Install this one first.

## What it does

- **hg bridge** (`core/api/hg_bridge.py`, `device_registry.py`): a small local API so Hermes robot tools can reach the robot: faces, lights, look, dance, music, DJ and rave mode, radio, meme previews. Token required, localhost and private LAN only.
- **Music**: real next / previous with a play history, voice shortcuts ("play <song>", "next song"), the DJ screen hooks (`plugins_func/functions/play_music.py`).
- **DJ set lists** (`core/handle/setlistHandler.py`), **internet radio** (`radioHandler.py`), **rave mode** (`raveHandler.py`), **Actor Mode** (`sceneHandler.py`). They only start working once you add their data (see [dj-mode](../dj-mode/), [radio](../radio/), [actor-mode](../actor-mode/)).
- **Screen text cleanup** (`core/utils/display_text.py`): her subtitles without stray punctuation or markup.
- **OTA lock-down** (`core/api/ota_handler.py`, `core/websocket_server.py`): optional OTA key and device allow-list.
- **English photo answers** (`core/providers/vllm/openai_en.py`): a vision provider that does not force Chinese replies and can turn model "thinking" off.
- **Fairy console** (`core/api/admin_handler.py`, `admin_page.html`, `admin_mobile.html`): the `/admin` web page from the fairy-stackchan project, localhost only by default.
- Small fixes in `core/connection.py`, `intentHandler.py`, `sendAudioHandle.py`, `dialogue.py`, `http_server.py` and two TTS providers.

All modified upstream files are listed in [UPSTREAM.md](./UPSTREAM.md); [patches/show-diff.sh](./patches/show-diff.sh) prints their full diff against the upstream commit. Everything else under `files/` is new.

## Requirements

- xiaozhi-esp32-server running in Docker from upstream commit **4745186c** ([UPSTREAM.md](./UPSTREAM.md)). Other commits may work, but this is the one these files were tested on.
- Firmware: any Nous-Girl build for the basics. DJ and rave mode need v3.6.7-dj5 or later, meme previews need **dj7**.

## Install

```
cd server
./install-addon.sh patched-server dry-run
./install-addon.sh patched-server apply
```

Then merge the keys from [config-snippet.yaml](./config-snippet.yaml) into your `data/.config.yaml` (by hand; never replace the whole file) and run `docker restart <your container>`.

## Verify

- `./install-addon.sh patched-server status` says `installed` for every file.
- `docker logs --tail 50 <container>` shows no Python errors after the restart.
- With the bridge on: `curl -s -H "Authorization: Bearer $HG_BRIDGE_TOKEN" http://127.0.0.1:8003/hg/api/tools` (on the server computer) lists the robot tools once the robot is connected.
- Say "next song" while a song plays: she skips.

## Rollback

```
./install-addon.sh patched-server rollback            # newest backup
./install-addon.sh patched-server rollback 20261029-101500
```

Then remove the keys you added to `data/.config.yaml`. Files that did not exist before are deleted again.

**Note:** these files live in the container, not in the image. If you recreate the container (`docker compose up` after a pull, or `docker rm`), run `apply` again, or build your own image ([UPSTREAM.md](./UPSTREAM.md#build-your-own-image)).

## License

The upstream project is MIT ([licenses/xiaozhi-esp32-server.txt](../../../licenses/xiaozhi-esp32-server.txt)). The Fairy console comes from fairy-stackchan, MIT ([licenses/fairy-stackchan.txt](../../../licenses/fairy-stackchan.txt)). Our changes are MIT like the rest of this repo.
