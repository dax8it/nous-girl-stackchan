# Actor Mode: she performs your script

You write a short play. She performs it **word for word**, with faces, lights, dances, head moves, music and pauses on cue. Your lines are in the script too: she waits until she hears you say them. Script lines never go to the language model, so she says exactly what you wrote, nothing more.

Everything on this page comes from the parser in the patched voice server, `core/handle/sceneHandler.py` ([patched-server](../server/addons/patched-server/)). A checker that uses that same parser is in [examples/actor-mode/](../examples/actor-mode/).

## Contents

- [What you need](#what-you-need)
- [Run a script by voice](#run-a-script-by-voice)
- [Where scripts live and how they are numbered](#where-scripts-live-and-how-they-are-numbered)
- [Write your first script](#write-your-first-script)
- [The text format](#the-text-format)
- [The JSON format](#the-json-format)
- [Cues: every allowed value](#cues-every-allowed-value)
- [Timing](#timing)
- [Limits](#limits)
- [Check a script before you run it](#check-a-script-before-you-run-it)
- [Common errors](#common-errors)

## What you need

- Your own voice server with the [patched-server](../server/addons/patched-server/) add-on. Any Nous-Girl firmware.
- Optional: [actor-mode](../server/addons/actor-mode/) for two example scripts, songs in the server music folder for `music` cues, [multilingual](../server/addons/multilingual/) for lines in Spanish, French, German or Portuguese.
- Actor Mode is on by default. To turn it off: `plugins.scene_mode.enabled: false` in `data/.config.yaml`, then restart the container.

## Run a script by voice

Wake her first ("Hey Nous-Girl"), then:

| Say | What happens |
|---|---|
| "action mode" or "actor mode" | She lists your scripts by number and waits about 20 s for a pick (a number or a word from the name). With only one script, she starts it. "Never mind" cancels. |
| "action mode two", "actor mode hello" | Starts that script directly. |
| "what scripts do you have?", "list your scripts" | Lists them and waits for a pick. |
| "start scene hello", "play the scene called hello" | Starts the script whose name is closest. |
| "read the script" | Plays the script named `script` (`script.txt`, `script.json`, ...) if there is one, otherwise the lowest-numbered script. |

She opens with **"Action!"**. During the script:

- When it's your line, say it. Close is good enough (missing words and small ad-libs are fine).
- **"next line"** (or "next", "skip", "keep going", "continue") skips your line.
- After **2** tries that don't match, she accepts what you said and carries on. Between tries, your line appears on her screen as a reminder.
- **"stop"** (or "cut", "stop scene", "end scene", "stop actor mode") ends it; she says **"That's a wrap!"**.
- While she is performing she ignores speech (except stop), so wait for her to finish.
- If nobody says anything for **3 minutes** while she waits for your line, the script ends by itself.

## Where scripts live and how they are numbered

- Folder: `data/scenes/` in the voice server (the `data` volume, so you can edit it on the host). `scripts/` is also searched.
- File types: `.txt`, `.scene` or `.md` (text format) and `.json` (JSON format).
- The **number** is the file name's prefix: `03-dragon.json` is "three", even if there is no 02. Files without a number get the next numbers after the highest one, alphabetically. The rest of the name is how she says it ("dragon").
- When a `.txt` and a `.json` have the same number (or the same name), only the `.json` is used.
- `README.md` is never used. `hello-test.txt` is left out of the menu but can still be started by name ("start scene hello test").
- No restart needed: she reads the folder every time.

## Write your first script

1. **Make a file** on the server computer: `data/scenes/03-my-first-show.txt`.
2. **Write her first line** with her label, then yours:
   ```
   NOUS: Hi! Welcome to my first show.
   YOU: What will you do?
   ```
3. **Add cues** on their own lines, in square brackets. A cue runs after the line before it has been spoken:
   ```
   [face: happy]
   NOUS: I will dance for you!
   [dance: wiggle]
   [pause: 3]
   ```
4. **End nicely:**
   ```
   [lights: rainbow]
   NOUS: Thank you for watching!
   ```
5. **Check it** (on any computer with this repo, no robot needed):
   ```
   python3 examples/actor-mode/validate_scene.py data/scenes/03-my-first-show.txt
   ```
   It prints every step and says **OK**, or tells you which cue would be skipped.
6. **Run it:** "Hey Nous-Girl", then "action mode three".
7. Want an action on the same line as the words, or a list of light colors? Switch to [JSON](#the-json-format). The full working example is [examples/actor-mode/hello-show.json](../examples/actor-mode/hello-show.json).

## The text format

One item per line. Blank lines and lines starting with `#` are ignored.

| Line | Meaning |
|---|---|
| `NOUS: text` | Her line. Labels that mean her: `NOUS`, `NOUS-GIRL`, `NOUS GIRL`, `NOUSGIRL`, `HER`, `ROBOT`, `NG`. |
| `NOUS[es]: text` | Her line in a language: `[en]`, `[es]`, `[fr]`, `[de]`, `[pt]` (two lowercase letters). |
| `YOU: text` | A person's line: she waits for it. Any other label works (`MOM`, `KID`, `SAM`): it must start with a capital letter and be capitals, numbers, spaces, `_` or `-`, up to 21 characters. |
| `text` with no label | Her line. |
| `[kind: value]` or `[kind value]` | A cue. See [cues](#cues-every-allowed-value). |

Her lines are split into sentences at `.`, `!` and `?`. Each sentence picks its own language when no `[xx]` is given.

## The JSON format

```json
{
  "title": "Hello show",
  "characters": ["NOUS", "YOU"],
  "lines": [
    {"action": "lights", "value": "blue"},
    {"speaker": "NOUS", "dialogue": "Hello!", "action": "face", "value": "happy"},
    {"speaker": "YOU", "dialogue": "Can you dance?"},
    {"speaker": "NOUS", "dialogue": "Watch this!", "action": "dance", "value": "wiggle"},
    {"action": "pause", "duration": 4}
  ]
}
```

**The document**

| Key | Type | Meaning |
|---|---|---|
| `lines` (or `script`) | list | The entries, in order. **Required.** The whole file may also be just this list. |
| `title`, `characters`, `comment` | any | Notes for you. Ignored by the server. |

**An entry** is one of these:

| Shape | Meaning |
|---|---|
| `"some text"` | Her line (a plain string). |
| `{"speaker", "dialogue", ...}` | A spoken line. `line` works instead of `dialogue`. |
| `{"action": ..., "value": ..., "duration": ...}` | One action on its own. |
| `{"action": [ {...}, {...} ]}` | Several actions in a row (`actions` works too). |
| `{"comment": "..."}` | Ignored. |

**Keys of a spoken line**

| Key | Type | Default | Meaning |
|---|---|---|---|
| `speaker` | string | `"NOUS"` | Her: `nous`, `her`, `nous-girl`, `nous girl`, `nousgirl`, `nous_girl`, `robot`, `ng` (any case). A person: `user`, `me`, `human`, `you`, `kid`, `guest` (shown as USER), or any other name (shown in capitals). |
| `dialogue` | string | | What is said. Her line: she says it. A person's line: she waits for it and never says it. Empty = actions only. |
| `lang` | string | `auto` | Her line only: `en`, `es`, `fr`, `de`, `pt`, or `auto` (guess per sentence). |
| `action` / `actions` | string, object or list | none | Actions attached to the line. On **her** line they run **before** she speaks. On a **person's** line they run **right after** she hears it. A string uses the line's own `value` / `duration` / `yaw` / `pitch` / `shown_as`. |
| `value`, `duration`, `seconds`, `yaw`, `pitch`, `shown_as` | | | Used by a string `action` on the same line. |

**Keys of an action**

| Key | Type | Default | Meaning |
|---|---|---|---|
| `action` (or `type`) | string | | `face`, `lights`, `dance`, `head`, `music`, `pause`, `lang`. Also accepted: `light`, `led`, `leds` (lights); `emote`, `expression`, `emotion` (face); `move`, `body` (dance); `song`, `sound` (music); `wait`, `sleep` (pause); `voice`, `language` (lang); `look`, `servo` (head). |
| `value` | string or list | `""` | What to do (tables below). A **list** runs each step in order, e.g. `["green", "purple", "rainbow"]`. A step may be an object `{"value", "duration", "yaw", "pitch"}`. |
| `duration` (or `seconds`) | number | none | A hold **after each step**, in seconds (0 to 60). For `music`, how long the song plays. For `pause`, the pause length (default 1). |
| `yaw`, `pitch` | number | 0, 45 | `head` only: an exact angle instead of a value. |
| `shown_as` | string | | `face` only: overrides `value` (used by tools that convert other scripts). |

`lang` as an action changes the language of **her following lines** until the next `lang` action (`auto` goes back to guessing). It makes no sound by itself.

## Cues: every allowed value

Values are not case-sensitive. Anything not in these lists is **skipped** (logged on the server, she carries on).

| Cue | Allowed values | What the robot does |
|---|---|---|
| `face` | `neutral`, `happy`, `sad`, `thinking`, `sleeping`, `pet`, `shake`, `speaking`, `listening`, `look_left`, `look_right`. Aliases: `love` (pet), `surprised`, `shocked`, `angry`, `mad` (shake), `think`, `sleepy`, `sleep`, `normal`, `smile`, `cry`, `left`, `right`. Spaces become `_`. | shows that face ([EMOTES.md](./EMOTES.md)). Not allowed here: `scowl`, `email`. |
| `lights` | colors: `red`, `orange`, `yellow`, `green`, `lime`, `teal`, `cyan`, `blue`, `navy`, `purple`, `violet`, `magenta`, `pink`, `white`, `warm white`, `gold`, `brown`, `gray`, or `#rrggbb`. Moods: `happy`, `calm`, `party`, `spooky`, `sleepy`, `focus`. Effects: `rainbow`, `breathe`, `rainbow fast` / `disco`. `off` / `dark` / `none`. `normal` / `auto` / `default` (back to automatic). | sets the LEDs at brightness 60 |
| `dance` | `wiggle`, `nod`, `disco`, `robot`, `sway` (empty = disco); `stop` / `off` | plays the preset twice |
| `head` | `center` (also `centre`, `home`, `forward`, `straight`, `middle`, empty), `left`, `right`, `up`, `down`, `look around` (`around`, `scan`), `nod` (`yes`). JSON only: `shake` / `no` (left, right, left, center, 0.4 s apart), or `yaw` -128..128 and `pitch` 3..87 | turns the head |
| `music` | a song name (fuzzy match against the server music folder, at least about half the letters must match), `random` / `any` / `something`, or `stop` / `off` / `end` | loads the song ([timing](#timing)) |
| `pause` | seconds, 0 to 60 | waits |
| `lang` | JSON only: `en`, `es`, `fr`, `de`, `pt`, `auto` | language for her next lines |

Head poses: `left` = yaw 40, `right` = yaw -40, both at pitch 45; `up` = pitch 70, `down` = pitch 20. Pitch 3 is level and her lowest.

## Timing

- Her lines are turned into speech and queued. A cue waits until the line **before** it has finished playing, then runs. Cues in a row run right after each other.
- Dances and head moves start and then run **while** the next lines play; add a `pause` (or `duration`) if you want her to wait for them.
- **Music** only plays during pauses: `{"action": "music", "value": "my song", "duration": 10}` loads the song and plays 10 seconds of it. A `[music: my song]` cue followed by `[pause: 10]` does the same in text. The song stops when she speaks again, when the script reaches a person's line, at `music stop`, or when it ends. Music makes her groove.
- `duration` on any other action adds a pause after it: `{"action": "lights", "value": ["green", "purple"], "duration": 1}` is green, wait 1 s, purple, wait 1 s.

## Limits

| What | Limit |
|---|---|
| A pause or duration | 0 to 60 s (bigger numbers are cut to 60) |
| Waiting for a person's line | 3 minutes with no speech, then the script ends |
| Picking from the menu | about 20 s after she finishes the list |
| Wrong tries on a person's line | 2, then she accepts and goes on |
| Line match | about half the important words (or a close spelling) |
| Head | yaw -128..128, pitch 3..87 |
| Text-format labels | capitals, up to 21 characters |
| Languages | en, es, fr, de, pt (needs the multilingual voice; otherwise her normal voice reads it) |

## Check a script before you run it

[examples/actor-mode/validate_scene.py](../examples/actor-mode/validate_scene.py) runs the **real** server parser and cue mapper on your file, offline:

```
python3 examples/actor-mode/validate_scene.py examples/actor-mode/hello-show.json
```

It prints every step (`SAY`, `WAIT`, `CUE`, `PAUSE`, `MUSIC`) and ends with **OK**, or lists each problem: invalid JSON, an unknown key, an unknown face, color, dance or cue, a language with no voice, an empty script. It exits with 1 on a problem, so you can use it in a script or CI. Music names can't be checked offline (they depend on your music folder).

## Common errors

| What happens | Why | Fix |
|---|---|---|
| She says "That script has a mistake in it." | The `.json` is not valid JSON (a missing comma or quote is the usual cause). | Run the checker; it shows the line and column. |
| She says "That script is empty." | No lines she could read: empty file, only comments, or a JSON file without `lines`. | Add lines; check the `lines` key. |
| A face, light or dance doesn't happen | The value isn't in the [allowed list](#cues-every-allowed-value) (for example `scowl`, `moonwalk`, `sparkly`), so it's skipped. | Run the checker; use a listed value. |
| She never waits for my line | The label is lowercase or mixed case in a text file (`You:`), so it was read as her line. | Text labels must be capitals: `YOU:`. In JSON, `speaker` can be any case. |
| She waits forever on my line | Speech recognition heard something too different. | Say "next line", or keep your lines short and plain. After 2 tries she goes on anyway. |
| Music doesn't play | No song name matched, or there was no pause after the `music` cue. | Use a word from the file name, and add a `duration` or `[pause: N]`. |
| The wrong script starts | Two files share a number, or the `.json` wins over the `.txt` with the same number. | Give every script its own number. |
| "action mode" goes to the brain instead | Actor Mode is off (`plugins.scene_mode.enabled: false`) or the patched server isn't installed. | Install [patched-server](../server/addons/patched-server/) and check the config. |
