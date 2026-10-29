# Upstream base

- Project: [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server), MIT License, Copyright (c) 2025 xinnan-tech. Full text: [licenses/xiaozhi-esp32-server.txt](../../../licenses/xiaozhi-esp32-server.txt).
- Commit: `4745186cead1b0f07ff7f8b83a79e499795b88eb` (2026-09-17).
- Paths in this add-on are relative to `main/xiaozhi-server/` in that repo, which is `/opt/xiaozhi-esp32-server/` inside the container.

## Modified upstream files

Print the full diff against that commit with [patches/show-diff.sh](./patches/show-diff.sh) (it needs a clone of upstream; the diff file itself is not stored here because upstream's own token-handling lines trip this repo's secret scanner).

```
core/api/ota_handler.py            core/handle/sendAudioHandle.py
core/api/vision_handler.py         core/http_server.py
core/connection.py                 core/providers/tts/gpt_sovits_v2.py
core/handle/intentHandler.py       core/providers/tts/openai.py
core/utils/dialogue.py             core/websocket_server.py
plugins_func/functions/play_music.py
```

To make a patched checkout instead of copying files into a container:

```
git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
cd xiaozhi-esp32-server && git checkout 4745186c
cp -R /path/to/server/addons/patched-server/files/. main/xiaozhi-server/   # modified and new files
git diff --stat                                                           # review what changed
```

## Build your own image

Copying files into a running container (what `install-addon.sh` does) is quick and easy to undo, but the files are gone if the container is recreated. For a permanent setup, build an image from the patched checkout above:

```
docker build -t xiaozhi-esp32-server:nous-girl -f Dockerfile-server .
```

Then set `image: xiaozhi-esp32-server:nous-girl` in your compose file ([docker-compose.example.yml](../../docker-compose.example.yml)).
