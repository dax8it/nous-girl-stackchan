# actor-mode: scripted scenes

## What it does

She performs a script word for word, with cues for faces, lights, dances, head moves, music and pauses. Lines with another speaker (YOU, MOM, KID) are yours: she waits until she hears you say it (a loose match), or until you say "next line". Script lines never go to the language model, so she says exactly what you wrote.

Voice: "action mode" (or "actor mode") lists the scripts by number and waits for a pick. "Action mode two" starts script 2. "What scripts do you have?" lists them. "Stop" ends with "That's a wrap!". She stops by herself after about 3 minutes with no input.

Code: [patched-server](../patched-server/) (`core/handle/sceneHandler.py`). This add-on adds two example scripts.

## Requirements

- [patched-server](../patched-server/) installed. Any firmware.
- For `[music: ...]` cues: songs in the server's music folder.
- For `NOUS[es]:` lines in other languages: [multilingual](../multilingual/).

## Install

```
./install-addon.sh actor-mode apply
```

### Writing scripts

Full guide with every field, value, limit and a first-script walkthrough: [docs/ACTOR-MODE.md](../../../docs/ACTOR-MODE.md). Check a script offline with [validate_scene.py](../../../examples/actor-mode/validate_scene.py).

Files go in `data/scenes/` and are numbered by their name prefix (`03-show.txt` is "three"). When a `.txt` and a `.json` share a number, only the `.json` is used.

**Text format** ([01-hello.txt](./files/data/scenes/01-hello.txt)):

```
# a comment
[lights: blue]
NOUS: Hello there!
YOU: What can you do?
[dance: wiggle]
[pause: 3]
NOUS[es]: Hola, amigos.
```

**JSON format** ([02-hello.json](./files/data/scenes/02-hello.json)): `{"title", "characters", "lines": [...]}`. Each line is `{"speaker", "dialogue", "lang", "action", "value", "duration"}`. Her actions fire before she speaks; actions on your lines fire right after she hears you.

| Cue | Values |
|---|---|
| `face` | neutral, happy, sad, thinking, sleeping, pet, shake, speaking, listening, look_left, look_right (plus aliases like love, surprised, sleepy) |
| `lights` | a color name, `rainbow`, `off` |
| `dance` | wiggle, nod, disco, robot, sway, `stop` |
| `head` | `center`, `left`, `right`, `up`, `down`, `look around`, `nod`, or `yaw` / `pitch` in JSON |
| `music` | a song name (fuzzy match), or `stop` |
| `pause` | seconds |

## Verify

Say "action mode one". She says "Action!" and starts the hello script.

## Rollback

```
./install-addon.sh actor-mode rollback
```

To switch Actor Mode off completely: `plugins.scene_mode.enabled: false` in `data/.config.yaml`, then restart the container.
