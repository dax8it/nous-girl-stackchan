# Faces and emotes: every animation, every name, what triggers it

Her face is one of **12 GIF animations**. Everything that changes her face (her mood while she talks, a tool call from the brain, a tap on her head, a shake) asks for a **name**, and the firmware turns that name into one of the 12 GIFs. This page lists all of them, every name that works, and what sets each one off. Facts come from the v3.6.7-dj7 firmware source and the voice server code.

## Contents

- [The 12 faces](#the-12-faces)
- [Every name the firmware understands](#every-name-the-firmware-understands)
- [What triggers each face](#what-triggers-each-face)
- [Talking faces and lip sync](#talking-faces-and-lip-sync)
- [Scowl and the anger mark](#scowl-and-the-anger-mark)
- [How the brain asks for a face](#how-the-brain-asks-for-a-face)
- [Lights that go with a face](#lights-that-go-with-a-face)
- [The classic StackChan face](#the-classic-stackchan-face)
- [Add your own emote](#add-your-own-emote)

## The 12 faces

Frame counts and timing are in [SPRITES.md](./SPRITES.md#the-12-animations).

| GIF | What it shows | Shown when |
|---|---|---|
| `neutral.gif` | resting, blinks | idle, connecting, and as the fallback for any unknown name |
| `happy.gif` | smiling, bouncy | most moods, music and dancing, giggle tap |
| `sad.gif` | sad, a tear | sad or crying moods |
| `thinking.gif` | eyes up, thinking dots | after she hears you, until the answer starts |
| `sleeping.gif` | eyes closed, Z z z | 120 s of neutral face with no music ("sleepy") |
| `pet.gif` | loving, hearts | head pat, loving moods, embarrassed |
| `shake.gif` | dizzy, shaking | you shake her, surprised, confused |
| `speaking.gif` | talking | while she talks, when lip sync is off |
| `listening.gif` | attentive, eyebrows up | while she listens to you |
| `look_left.gif`, `look_right.gif` | glance to one side and back | tap on the last head pad (random side) |
| `email.gif` | a letter arrives | while she sends an email (brain asks for it) |

## Every name the firmware understands

Any GIF file name (without `.gif`) is a valid name. On top of that, these aliases are built into the firmware. If the first choice is missing from the face set, the next one is used; if nothing matches, she shows **neutral**.

| Name | Shows |
|---|---|
| `neutral`, `happy`, `sad`, `thinking`, `sleeping`, `pet`, `shake`, `speaking`, `listening`, `look_left`, `look_right`, `email` | that GIF |
| `loving`, `love`, `kissy` | pet (else happy) |
| `embarrassed` | pet (else sad) |
| `doubtful`, `dizzy`, `confused`, `shocked` | shake (else neutral) |
| `surprised` | shake (else happy) |
| `sleepy` | sleeping (else neutral) |
| `talking` | speaking (else neutral) |
| `mail`, `sending` | email (else happy) |
| `laughing`, `funny`, `winking`, `cool`, `confident`, `relaxed`, `delicious`, `silly` | happy |
| `crying` | sad |
| `angry` | neutral **plus the red anger mark** for 2.5 s |
| `scowl` | neutral **plus angry eyebrows and the anger mark, held on** |

**Good to know:** the face folder's `_emote_aliases.json` is **not** read by the firmware. The alias table above lives in the firmware code (`stackchan_geometry_display.cc`). The JSON is used by the face-building tools.

## What triggers each face

| Trigger | Face | How long |
|---|---|---|
| Wake word or tap to talk: **listening** | `listening` | until you stop talking |
| Your words were recognized, the brain is working | `thinking` | until the reply starts |
| She talks | the mood's talking face ([below](#talking-faces-and-lip-sync)) | while she talks |
| Mood from the reply (the server's emoji tag) | that mood's face | until the next change |
| Back to idle or connecting | `neutral` | |
| Neutral for **120 s** in a row, no music | `sleepy` (sleeping.gif) | until anything else happens |
| Music or a dance is playing ("party") | `happy`, no lip sync; `listening` and `thinking` still show | while it plays |
| Head pad, **first pad** tap | `laughing` (happy) and two quick nods | 3 s, then back |
| Head pad, **middle pad** tap, or a still **hold** of about a second | `loving` (pet, hearts) | 3.5 s, then back |
| Head pad, **last pad** tap | `look_left` or `look_right` (random) and a peek | 2.5 s, then back |
| You **shake** her (motion sensor) | `doubtful` (shake.gif) and a head wobble | until the shaking stops |
| A tool call from the brain, Actor Mode or a DJ cue | the name it asks for | until the next change |
| An error or alert from the firmware | the alert's emotion | until it's dismissed |

Head taps are ignored while music or a dance plays, and while she's dizzy. Which pad is "first" depends on how the head sensor is mounted; try each one.

## Talking faces and lip sync

While she talks, the firmware gets `speaking:<mood>` and picks a face with a moving mouth (Settings > **Screen** > lip sync):

| Mood while talking | Face | Mouth |
|---|---|---|
| `sad`, `crying` | sad | no smile |
| `thinking` | thinking | no smile |
| `loving`, `love`, `kissy`, `pet`, `embarrassed` | pet | no smile |
| `angry` | neutral | no smile, plus the anger mark |
| `scowl` | neutral | no smile, angry eyebrows and held anger mark |
| `neutral`, `sleepy`, `confused`, `doubtful`, `shocked`, `surprised`, `dizzy` | neutral | no smile |
| anything else (happy, funny, cool, ...) | neutral | smiling mouth |

With lip sync off, a talking face is `speaking.gif`.

## Scowl and the anger mark

Not a GIF: the firmware draws them on top of the neutral face.

- **Anger mark:** a small red mark at the top right of her face. `angry` shows it for **2.5 s**.
- **Scowl:** `scowl` draws two thick black eyebrow lines angled down over her eyes (positions per face in `hg_face_layout.h`, see [SPRITES.md](./SPRITES.md#eyes-scowl-and-anger-mark)) and keeps the anger mark **on** until the face changes. The LEDs pulse red. It's a playful "fierce" look for DJ sets and games, added in v3.6.7-dj.
- Ask for it with `robot_face` / `self.face.show` emotion `scowl`, or a DJ set list cue `{"action": "face", "value": "scowl", "duration": 2}`. Actor Mode scripts **can't** use scowl (their face list doesn't include it).

## How the brain asks for a face

1. **Emoji in the reply (any brain).** The voice server looks for the first of these emoji in each reply and sends that mood to the robot (default `happy`): 😂 funny, 😭 crying, 😠 angry, 😔 sad, 😍 loving, 😲 surprised, 😱 shocked, 🤔 thinking, 😌 relaxed, 😴 sleepy, 😜 silly, 🙄 confused, 😶 neutral, 🙂 happy, 😆 laughing, 😳 embarrassed, 😉 winking, 😎 cool, 🤤 delicious, 😘 kissy, 😏 confident (from upstream `core/utils/textUtils.py`). The emoji is not spoken.
2. **A tool call (Hermes).** `robot_face(emotion)` from [stackchan-tools](../server/addons/stackchan-tools/) calls the robot tool `self.face.show`. Allowed: `neutral, happy, sad, thinking, sleeping, pet, shake, speaking, listening, look_left, look_right, scowl`. The robot tool itself accepts any name of letters, numbers, `_` and `-` (40 characters max), plus `email`.
3. **SOUL.md** tells her when: for example "show your thinking face while sending an email, happy when it's sent", or "use robot_face to show what you feel, don't overdo it" ([stackchan-tools soul.md](../server/addons/stackchan-tools/soul.md)).
4. **Scripts:** Actor Mode `face` cues ([ACTOR-MODE.md](./ACTOR-MODE.md)) and DJ set list cues ([dj-mode](../server/addons/dj-mode/)).

## Lights that go with a face

The LEDs follow some faces by themselves (unless you set the lights by hand):

| Face name | Light | How long |
|---|---|---|
| `thinking` | amber pulse | until the state changes |
| `happy`, `laughing` | warm yellow | 2 s |
| `loving`, `pet` | pink pulse | 3 s |
| `sad`, `crying` | dim blue pulse | 3 s |
| `doubtful`, `dizzy` | white pulse | 1.5 s |
| `email` / `sending` | green / green pulse | 3 s |
| `sleepy`, `sleeping` | very dim violet pulse | until the state changes |
| `scowl` | red pulse | 3 s |

## The classic StackChan face

The power key switches to the classic line-drawn StackChan face ([SETTINGS.md](./SETTINGS.md#screen-gestures-and-buttons-outside-settings)). It has fewer expressions, so names are mapped to its own set (happy, sad, angry, sleepy, doubt, neutral), with extras: hearts and a blush on a head pat, a dizzy spiral on a shake, an anger mark on `angry`, sweat on `sad`, and "Zzz…" when sleepy. Scowl eyebrows and lip sync are GIF-face only.

## Add your own emote

**Replace a face** (easiest): draw a new GIF with the same file name, for example a new `happy.gif`, and rebuild the face assets as in [SPRITES.md](./SPRITES.md). Every trigger above keeps working.

**Add a new name**, for example `wave`:

1. Make `wave.gif` (same size and style as the others, see [SPRITES.md](./SPRITES.md); keep it small, the 12 faces share the assets partition with everything else).
2. Put it next to the other GIFs in the firmware tree's `fairy-assets/`, rebuild the assets and flash them ([SPRITES.md](./SPRITES.md#build-and-flash)). The file name becomes the emotion name. Names: letters, numbers, `_`, `-`.
3. It works right away with the robot tool `self.face.show {"emotion": "wave"}`.
4. To let the brain use it, add `"wave"` to `EMOTIONS` in `stackchan_mcp.py` ([stackchan-tools](../server/addons/stackchan-tools/)) and mention it in SOUL.md. For Actor Mode, add it to `FACES` in the patched server's `core/handle/sceneHandler.py`.
5. Optional: a new alias (say `greeting` -> `wave`) goes in `kGifAliases` in `stackchan_geometry_display.cc`, which needs a firmware build. An emoji mood only works for the names in the emoji list above.
