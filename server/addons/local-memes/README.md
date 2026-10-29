# local-memes: classic memes made on your computer

## What it does

"Make a meme about homework vs. naps." She picks a classic template (Drake, distracted boyfriend, two buttons, expanding brain and about 56 more), writes short captions, and an MCP server renders them with Pillow in well under a second. No cloud, no API key, no AI image model. Each meme is saved in `~/NousGirl-memes/` three ways: the original (WebP) plus a 320x240 JPEG and PNG for her screen, so [meme-previews](../meme-previews/) can show it.

Two tools: `list_templates(query)` and `make_meme(template, top_text, bottom_text, lines, title)`.

Kid-safety in code (the SOUL.md rules are the main guard): captions with swear words, violence, drugs, politics or a name from `NG_MEME_BLOCKED_NAMES` are refused.

## Requirements

- Hermes Agent and [uv](https://docs.astral.sh/uv/). Works on macOS, Linux and Windows.
- A bold meme font: Impact (installed on macOS) or [Anton](https://fonts.google.com/specimen/Anton) (SIL Open Font License); put `Anton-Regular.ttf` in `~/NousGirl-meme-templates/fonts/` or set `NG_MEME_FONT`.
- The blank template pictures. **They are not in this repo** (they are stills from films, TV and photos owned by others). [tools/build_templates.py](./tools/build_templates.py) downloads them once for your private use; the text box positions come from [memegen.link](https://github.com/jacebrowning/memegen) (MIT).

## Install

```
./install-addon.sh local-memes apply
uv run --no-project --python 3.12 --with pillow --with pyyaml python addons/local-memes/tools/build_templates.py
```

(`build_templates.py` also rewrites `hermes/templates.json` in the repo copy; copy it again with `apply` if you change it.) Then:

1. Add [config-snippet.yaml](./config-snippet.yaml) to your profile `config.yaml` with full paths, and set `NG_MEME_BLOCKED_NAMES`.
2. Add [soul.md](./soul.md) to SOUL.md.
3. Restart the gateway.

## Verify

```
cd ~/.nous-girl/hermes/local-memes
uv run --no-project --python 3.12 --with "mcp>=1.2,<2" --with pillow python nousgirl_localmeme_mcp.py --selftest
uv run --no-project --python 3.12 --with "mcp>=1.2,<2" --with pillow python nousgirl_localmeme_mcp.py --make drake "doing homework" "building a robot"
```

The self-test prints `PASS` lines; `--make` prints JSON with the saved paths. Then ask her for a meme.

## Rollback

```
./install-addon.sh local-memes rollback
```

Remove the config block and SOUL.md lines, restart the gateway. Saved memes and templates stay in your home folder until you delete them.
