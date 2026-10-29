# dj-mode: DJ set lists

## What it does

DJ mode turns her screen into a music display (level meter, song title) and makes her groove on the beat. Set lists play your songs in order with her spoken intros and timed cues: faces, lights, dances, meter colors, pauses.

The code is in [patched-server](../patched-server/) (`core/handle/setlistHandler.py`, `plugins_func/functions/play_music.py`). This add-on adds an example set list and the SOUL.md lines.

## Requirements

- [patched-server](../patched-server/) installed.
- Firmware v3.6.7-dj5 or later.
- Songs in the server's music folder (`plugins.play_music.music_dir`, `./music` by default). Audio streams from the server, so no SD card is needed.
- For voice control through Hermes: [stackchan-tools](../stackchan-tools/).

## Install

```
./install-addon.sh dj-mode apply
```

Then edit `data/setlists/01-example-set.json` on the host (it is in the `data` volume) so each `track` names one of your songs. No restart needed for set list edits.

### The set list format (`dj-setlist/1`)

| Key | What it is |
|---|---|
| `title` | the name she says in the menu |
| `defaults` | `face`, `lights`, `groove` (true/false), `gap` (seconds between songs), `vu` (`on`, `color`) |
| `loop`, `shuffle` | true or false |
| `songs[]` | `id` (unique), `track` (song file name; exact first, then a fuzzy match), optional `start` / `end` seconds |
| `intro`, `outro` | lines she says: `{"speaker": "NOUS", "dialogue": "...", "lang": "en"}` |
| `cues[]` | `{"at": seconds, "action": "face|lights|dance|head|pause|voice|vu", "value": ..., "duration": seconds}` |

Meter colors (`vu`): `purple`, `blue`, `gold`, `warm`, `rainbow`, `classic`, `on`, `off`, `pulse`. Files are numbered by their name prefix: `01-...` is "one".

## Verify

Say "DJ mode". She lists your set lists. Say "one": the first song starts with her intro.

## Rollback

```
./install-addon.sh dj-mode rollback
```

This removes the example set list (or restores the one you had). Remove the SOUL.md lines by hand.
