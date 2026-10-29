# stackchan-tools: robot tools for Hermes

## What it does

A small stdio MCP server that gives the Hermes brain 15 robot tools. Each tool calls the hg bridge in the patched voice server, which passes an allow-listed device command to the robot.

| Tool | What it does |
|---|---|
| `robot_lights` | color, mood, rainbow cycle, off (brightness up to 80) |
| `robot_face` | show an emotion |
| `robot_look` | turn the head (yaw -90..90, pitch 3..85) |
| `robot_what_do_you_see` | take one photo and describe it (at most one photo per 5 s) |
| `robot_dance` | presets wiggle, nod, disco, robot, sway; saved dances only if named in `STACKCHAN_SAVED_DANCES` |
| `robot_play_music`, `robot_music_skip` | songs from the server music folder; next and previous |
| `robot_music_sd` | songs from the robot's SD card |
| `robot_dj_mode`, `robot_rave_mode` | DJ and rave mode on or off |
| `robot_radio` | internet radio: play, next, previous, stop, list, status, resume |
| `robot_follow_faces` | head follows a face, on or off |
| `robot_who_is_here` | first name of an enrolled person, or "unknown"; read only. Needs a server-side face ID setup that is **not** in these add-ons (it stores face data of real people, so it stays private). Without it the answer is always "unknown". |
| `robot_show_meme` | show a saved meme on her screen for about 5 s (a 160x120 PNG, 64 KB max, made in memory) |
| `robot_status` | is she online, which tools she has |

Limits are enforced in the script: numbers outside a range are clamped, unknown strings are refused, there is no generic pass-through.

## Requirements

- [patched-server](../patched-server/) with `hg_bridge.enabled: true` and a token.
- [Hermes Agent](https://hermes-agent.nousresearch.com/) with a profile for her ([docs/HERMES.md](../../../docs/HERMES.md)), and [uv](https://docs.astral.sh/uv/).
- Firmware: any Nous-Girl build. DJ and rave tools need v3.6.7-dj5+, `robot_show_meme` needs dj7, `robot_follow_faces` v3.6.3+.
- The Hermes computer must reach the voice server's HTTP port (8003 by default). The bridge only answers localhost and private LAN addresses.

## Install

```
./install-addon.sh stackchan-tools apply
```

This copies `stackchan_mcp.py` to `~/.nous-girl/hermes/stackchan-tools/` (change with `NG_ADDONS_DIR`). Then:

1. Add the block from [config-snippet.yaml](./config-snippet.yaml) under `mcp_servers:` in your profile's `config.yaml`, with the full path to the file.
2. Add `HG_BRIDGE_TOKEN=...` to the profile's `.env` ([.env.example](../hermes-profile/.env.example)). Same value as `hg_bridge.token` on the server.
3. Add [soul.md](./soul.md) to her `SOUL.md`.
4. `hermes -p <profile> gateway restart`.

## Verify

- Offline check, no robot needed: `uv run --no-project --python 3.12 --with "mcp>=1.2,<2" --with httpx --with pillow python -c "import sys; sys.path.insert(0, '$HOME/.nous-girl/hermes/stackchan-tools'); import stackchan_mcp"` exits without errors.
- Say "make a happy face": her face changes. "Are you online?" uses `robot_status`.

## Rollback

```
./install-addon.sh stackchan-tools rollback
```

Remove the `stackchan` block from `config.yaml`, the SOUL.md lines and `HG_BRIDGE_TOKEN` from `.env`, then restart the gateway.
