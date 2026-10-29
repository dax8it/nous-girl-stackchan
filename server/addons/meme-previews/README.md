# meme-previews: memes on her screen

## What it does

"Show me!" After she makes a meme, `robot_show_meme` shows it on her screen for about 5 seconds, then her face comes back. A tap hides it early. The Hermes tool makes a **160x120** PNG in memory (opaque RGB, letterboxed on black) from the saved 320x240 copy and sends it to the hg bridge; the bridge refuses anything bigger than 160x120 or 64 KB. The firmware draws it at the same size a 320x240 preview used to have, with a quarter of the memory. Saved files are never changed.

The code is in [patched-server](../patched-server/) (`hg_bridge.py`, `POST /hg/api/show_meme`) and [stackchan-tools](../stackchan-tools/) (`robot_show_meme`). This add-on only adds the SOUL.md lines.

## Requirements

- Firmware **v3.6.7-dj7**. (dj6 can run out of memory and restart while showing a preview.)
- [patched-server](../patched-server/), [stackchan-tools](../stackchan-tools/), and saved memes from [local-memes](../local-memes/) in `~/NousGirl-memes/`. If Hermes and the memes are on different computers, set `NG_MEMES_DIR` in the stackchan block.

## Install

Add [soul.md](./soul.md) to SOUL.md and restart the gateway.

## Verify

Ask "make a meme about Mondays and show me". The meme appears for about 5 seconds.

## Rollback

Remove the SOUL.md lines. To block it at the server too, remove `robot_show_meme` from the `tools.include` list of the stackchan block.
