# radio: internet radio

## What it does

"Play the radio", "play Drone Zone", "put on some jazz", "next station", "stop the radio". The server streams the station through her speaker with the DJ screen. If a station does not answer, it moves on to the next one and says so.

Code: [patched-server](../patched-server/) (`core/handle/radioHandler.py`). This add-on adds the station list and SOUL.md lines.

## Requirements

- [patched-server](../patched-server/) installed. Firmware v3.6.7-dj3 or later.
- `ffmpeg` in the container (the upstream image has it).
- For voice control through Hermes: [stackchan-tools](../stackchan-tools/) (`robot_radio`).

## Install

```
./install-addon.sh radio apply
```

### Stations (`radio-stations/1`)

`data/radio/stations.json` ships with 11 free, listener-supported or public stations: SomaFM (Groove Salad, Drone Zone, Secret Agent, Space Station Soma, Fluid, Indie Pop Rocks), Radio Paradise (main and Mellow Mix), Radio Swiss Jazz, Radio Swiss Classic and AgentRadio. Each entry:

```json
{"id": "groovesalad", "name": "Groove Salad", "aliases": ["groove salad", "chill beats"], "url": "https://...", "genre": "ambient, downtempo", "by": "SomaFM", "notes": "..."}
```

`default` is the station for a plain "play the radio". Add your own stations with a direct stream URL (MP3 or AAC). The streams are for listening only; respect each station's terms.

## Verify

Say "play the radio". She says the station name and music starts. "Next station" switches.

## Rollback

```
./install-addon.sh radio rollback
```

Remove the SOUL.md lines by hand.
