# rave-mode: rave mode by voice

## What it does

DJ mode with the whole screen changing color to the music, the lights and her dancing on the beat. "Rave mode", "start a rave with Groove Salad", "rave mode off" (music keeps playing), "stop the rave" (music stops too). Colors are soft on purpose: no strobe, no fast red flashes.

Code: [patched-server](../patched-server/) (`core/handle/raveHandler.py`) handles the words on the server, and [stackchan-tools](../stackchan-tools/) has the Hermes tool `robot_rave_mode`. This add-on only adds the SOUL.md lines.

## Requirements

- Firmware v3.6.7-dj5 or later (rave needs the DJ screen).
- [patched-server](../patched-server/), songs or [radio](../radio/).

## Install

Add [soul.md](./soul.md) to your SOUL.md and restart the Hermes gateway. `./install-addon.sh rave-mode dry-run` prints the same steps.

## Verify

Play a song, then say "rave mode". The screen fills with color on the beat. "Rave mode off" goes back to the DJ screen.

## Rollback

Remove the SOUL.md lines. To switch the server words off too, set `plugins.play_music.hg_voice_shortcut: false` (that also turns off the other music voice shortcuts).
