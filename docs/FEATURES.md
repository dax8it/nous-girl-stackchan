# Nous-Girl features: turn them on, use them, build them yourself

Every feature, in the same order each time:

1. **What it does**, in one or two lines.
2. **Turn it on**: switches in Settings, or what you need installed.
3. **Use it**: what to say or tap.
4. **Set it up yourself**: what runs where on your own server and Hermes profile, with the tools and config keys involved.
5. **Turn it off / undo.**

Every Settings switch is explained in detail in [SETTINGS.md](./SETTINGS.md). Which computer can run what: [PLATFORMS.md](./PLATFORMS.md). Firmware versions: [VERSIONS.md](../VERSIONS.md).

> **About add-ons.** Many features live in the voice server or the Hermes brain, not the firmware. They are packaged as **add-ons** in [server/](../server/README.md), each with install, check and undo steps, and installed with `./install-addon.sh <name> apply` (undo: `./install-addon.sh <name> rollback`). Most server features need [patched-server](../server/addons/patched-server/) first. Features marked **robot only** work with just the public firmware.

> Voice examples assume you wake her first with "Hey Nous-Girl" (or "Hey Jarvis"), or tap the screen.

## Quick map

| Feature | Runs on | Needs your own server? | Public now? |
|---|---|---|---|
| [Wake words](#wake-words) | robot | no | yes |
| [Voice chat](#voice-chat) | robot + voice server | any xiaozhi server, or the official cloud | yes |
| [Faces, lip sync and moods](#faces-lip-sync-and-moods) | robot | no | yes |
| [Lights](#lights) | robot | voice control needs Hermes robot tools | yes (Settings) |
| [Head touch and moves](#head-touch-and-moves) | robot | no | yes |
| [Dance](#dance) | robot | voice control needs Hermes robot tools | yes (Settings) |
| [Music](#music) | robot (SD) or server | SD: no. Computer songs: yes | SD and plain server music: yes. Real next/previous: patched server |
| [DJ mode](#dj-mode) | robot + patched server | **yes** | yes ([dj-mode](../server/addons/dj-mode/)) |
| [Rave mode](#rave-mode) | robot + patched server | **yes** | yes ([rave-mode](../server/addons/rave-mode/)) |
| [Internet radio](#internet-radio) | patched server | **yes** | yes ([radio](../server/addons/radio/)) |
| [Multilingual voice](#multilingual-voice) | server | **yes** | yes ([multilingual](../server/addons/multilingual/)) |
| [Camera and "what do you see"](#camera-and-what-do-you-see) | robot + server vision model | **yes** | firmware yes |
| [Follow faces](#follow-faces) | robot | voice control needs Hermes | yes (Settings) |
| [Hermes Agent brain and memory](#hermes-agent-brain-and-memory) | your computer | **yes** | yes (docs) |
| [SOUL.md personality](#soulmd-personality) | Hermes | **yes** | yes (docs) |
| [Kid-safe by default](#kid-safe-by-default) | Hermes + code guards | **yes** | yes (rules and add-ons) |
| [AgentMail email](#agentmail-email) | Hermes | **yes** | config shown here |
| [Local meme maker](#local-meme-maker) | Hermes | **yes** | yes ([local-memes](../server/addons/local-memes/)) |
| [Meme previews on her screen](#meme-previews-on-her-screen) | robot + patched server | **yes** | yes ([meme-previews](../server/addons/meme-previews/)) |
| [Email this meme](#email-this-meme-email_meme) | Hermes | **yes** | yes ([email-meme](../server/addons/email-meme/)) |
| [Actor Mode](#actor-mode) | patched server | **yes** | yes ([actor-mode](../server/addons/actor-mode/), [guide](./ACTOR-MODE.md)) |
| [Fun pack](#fun-pack-games-stories-and-kid-memory) | Hermes | **yes** | yes ([fun-pack](../server/addons/fun-pack/)) |
| [Sleep and power](#sleep-and-power) | robot | no | yes |
| [Crash report](#crash-report) | robot | no | yes |
| [Wi-Fi and phone hotspot](#wi-fi-and-phone-hotspot) | robot | no | yes |
| [Flashing, updates and rollback](#flashing-updates-and-rollback) | your computer + USB | no | yes |
| [IR remote](#ir-remote-experimental) | side build | no | no |

---

## How the pieces talk (read once)

```
You speak -> robot (wake word, mic) -> voice server (speech-to-text) -> brain (Hermes or a model)
brain answers -> voice server (text-to-speech) -> robot speaker, face, lights
brain wants to move her -> stackchan MCP server -> voice server "hg bridge" -> robot tool (self.led.light, ...)
```

- **Robot tools** are small commands built into the firmware, like `self.face.show`, `self.led.light` or `self.dance.preset`. The voice server can call them over the robot's connection.
- **The hg bridge** is a small add-on in the patched voice server. It lets Hermes call an allow-listed set of robot tools over HTTP (`POST /hg/api/device_call`, LAN only, with a bearer token you make up). It also plays songs (`POST /hg/api/play_music`).
- **The stackchan MCP server** is a small Python program Hermes runs. It turns the brain's tool calls (`robot_face`, `robot_dance`, ...) into bridge calls, with safety limits (for example, head angles and light brightness are capped). Setup: [HERMES.md](./HERMES.md#robot-tools-reference).

---

## Wake words
- **What it does:** she listens for "Hey Nous-Girl" (custom phrase), "Hey Jarvis" (ready-made model), and optionally "Hey YuB". All of it runs on the robot, offline.
- **Turn it on:** on by default. Settings > **Wake words**: master switch, each word, sensitivity. Then **Restart to apply**.
- **Use it:** say the wake word, wait for her listening face, then talk. To stop her while she talks, **tap the screen**.
- **Set it up yourself:** nothing on the server. Words are saved on the robot (`hg` / `ww_*` keys, see [SETTINGS.md](./SETTINGS.md#where-settings-are-stored)). Changing the custom phrase itself means a firmware build: the phrases are in `main/audio/wake_words/custom_wake_word.cc`, spelled the way ESP-SR MultiNet's English model says them ("hey noose girl").
- **Good to know:** "Hey Nous-Girl" keeps one CPU core busy while she's idle. On dj7 you may see occasional choppy audio; turning it off and using "Hey Jarvis" fixes that. Interrupting her with the wake word was tried and removed (no echo cancelling on this board).
- **Turn it off:** Settings > Wake words > Wake words on, off, then Restart to apply.
- **Added in:** v3; v3.3 made "Hey Nous-Girl" run only while she's idle.

## Voice chat
- **What it does:** you talk, she answers out loud, with her words on screen.
- **Turn it on:** she needs a voice server address in Settings > **Server** ([first boot](./SELF-HOSTING.md#first-boot-step-by-step)).
- **Use it:** wake word or tap, then talk normally. She listens until you stop speaking (Silero VAD on the server decides when).
- **Set it up yourself:** run [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) in Docker ([SELF-HOSTING.md](./SELF-HOSTING.md)). In its `data/.config.yaml`, `selected_module` picks the parts: `VAD` (SileroVAD), `ASR` (speech-to-text), `LLM` (the brain), `TTS` (her voice). The maintainer uses Parakeet on MLX for speech-to-text and Phonon for the voice; any provider the server supports works.
- **Turn it off:** clear the server address with Factory reset, or just turn off the server.

## Faces, lip sync and moods
- **What it does:** an animated face that follows her state (listening, thinking, speaking, sleeping), a mouth that moves with her voice, and moods: happy, sad, thinking, pet (loving), shake (dizzy), look left/right, email, scowl.
- **Turn it on:** automatic. Settings > **Screen**: Lip sync, Vowel mouth shapes. The **power key** (short press) switches between the drawn starter face and the classic StackChan line face.
- **Use it:** "look happy", "make a sad face", "look left". (Needs the robot tools in Hermes.)
- **Set it up yourself:**
  1. Pick a face when you flash: robot, boy or girl ([VERSIONS.md](../VERSIONS.md#how-to-flash)). Your own art: [SPRITES.md](./SPRITES.md).
  2. For voice control, add the stackchan MCP server to Hermes with `robot_face` in its tool list ([HERMES.md](./HERMES.md#robot-tools-reference)). It calls the robot tool `self.face.show` with `emotion` = neutral, happy, sad, thinking, sleeping, pet, shake, speaking, listening, look_left, look_right or scowl (the robot tool itself also takes `email` and the aliases).
  3. Brain can also switch the face style with the robot tool `self.face.style` (`girl` = drawn face, `classic`).
- **Good to know:** every face, every alias, what triggers each (moods, touch, shake, listening, speaking) and how to add your own: [EMOTES.md](./EMOTES.md).
- **Turn it off:** the same Screen switches. A face style change is saved until you press the power key again.
- **Added in:** v1 (faces), v3 (lip sync), v3.5 (vowel mouths), v3.6.7-dj (scowl).

## Lights
- **What it does:** 12 RGB LEDs: colors, moods, rainbow, talking lights that follow her voice, and beat-synced colors in DJ mode.
- **Turn it on:** Settings > **Lights** (On, Off, Auto, colors, moods, rainbow). Settings > Screen > **Talking lights**.
- **Use it:** "turn your lights blue", "party lights", "rainbow lights", "lights off", "lights back to normal".
- **Set it up yourself:** for voice control, include `robot_lights` in the stackchan MCP server. It calls `self.led.light` with `action` (on, off, color, mood, cycle, normal), `color` (a name like teal or warm white, or a hex code), `mood` (happy, calm, party, spooky, sleepy, focus), `cycle` (rainbow, breathe), `speed` and `brightness`. The MCP server caps brightness at 80.
- **Turn it off:** Settings > Lights > **Auto** (back to automatic colors), or "lights back to normal".

## Head touch and moves
- **What it does:** a 3-pad head sensor (tap front, middle or back, hold, swipe), idle looking around, and a dizzy head shake if you jolt her.
- **Turn it on:** automatic. Settings > **Head**: rest tilt, Go home, Look around when idle, Swap head swipe direction.
- **Use it:** swipe her head forward for the next song or dance, back to stop. "Look at me", "look up", "stop moving around".
- **Set it up yourself:** voice control of the head uses `robot_look` (calls `self.servo.set_angles`; the MCP server limits yaw to -90..90 and pitch to 3..85 degrees, where 3 is level).
- **Turn it off:** Settings > Head > Look around when idle, off.

## Dance
- **What it does:** preset dances (wiggle, nod, disco, robot, sway) with head, lights and face; dances you record with the K151-R remote; grooving to music.
- **Turn it on:** Settings > **Dance** for presets and recording. Settings > Music > **Dance to music** for grooving (on by default).
- **Use it:** "do a dance", "do the robot dance", "dance to this". Record: Settings > Dance > **Learn a dance**, steer her with the remote, then **Stop + save** (up to 60 s, saved on the SD card).
- **Set it up yourself:** include `robot_dance` in the stackchan MCP server. Actions: `preset` (name, loops 1 to 5, speed 50 to 150), `play` (a saved dance; only names you list in `STACKCHAN_SAVED_DANCES` are allowed), `stop`, `list`. Robot tools behind it: `self.dance.preset`, `self.dance.play`, `self.dance.stop`, `self.dance.list`, and `self.dance.groove` for music.
- **Turn it off:** Settings > Dance > **Stop**. Settings > Music > Dance to music, off.

## Music
- **What it does:** plays songs from the **SD card** (on the robot) or from a **music folder on your computer** (through the voice server).
- **Turn it on:**
  - SD card: copy MP3s into a `/music` folder on a FAT32 microSD card (32 GB or smaller) and put it in.
  - Computer: put MP3s in the server's music folder (next section).
- **Use it:** "play music", "play <song name>", "next song", "previous song", "stop". On screen, while a song plays: **swipe right to left = next, left to right = previous**. SD songs: Settings > **Music** lists them; tap one, or **Random**.
- **Set it up yourself (computer songs):**
  1. In your server's `docker-compose.yml`, mount a folder into the container: `./music:/opt/xiaozhi-esp32-server/music`.
  2. In `data/.config.yaml`, under `plugins:`, set `play_music:` with `music_dir: "./music"` and `music_ext` (for example `.mp3`).
  3. Make sure the brain can call the server's `play_music` function, or (with Hermes) include `robot_play_music` in the stackchan MCP server.
  4. `docker compose restart`.
  The stock server picks songs. Real next/previous with a play history, and the swipe controls for computer songs, come from the [patched-server](../server/addons/patched-server/) add-on.
- **Turn it off:** "stop", or Settings > Music > **Stop** for SD songs.
- **Added in:** v1; v3.6.7-dj (real next/previous, swipes); dj5 (reliable swipes).

## DJ mode
- **What it does:** she DJs your computer's songs: intros, set lists with timed face, light and dance cues, a 13-band equalizer on screen behind her face, colors on the beat, matching LEDs, and head grooving.
- **Needs:** firmware v3.6.7-dj5 or later, **your own patched server** ([patched-server](../server/addons/patched-server/) and [dj-mode](../server/addons/dj-mode/) add-ons), Hermes robot tools ([stackchan-tools](../server/addons/stackchan-tools/)).
- **Turn it on:** Settings > Music > **DJ mode**, or say "DJ mode".
- **Use it:** "DJ mode" or "be my DJ" (she lists set lists and waits for your pick), "DJ mode 1", "next", "previous", "DJ mode off", "stop DJ". Tap to pause a set, "continue" to resume. Swipe for next/previous.
- **Set it up yourself:** install the add-ons above. What they change:
  1. The server's `play_music.py`: per-robot play history (real next/previous), "DJ mode" / "DJ mode off" by voice before the brain sees it, DJ intros, and the song title sent to the robot.
  2. A new set list handler reading JSON files from the server's `data/setlists/`.
  3. The hg bridge: allows `self.dj.mode`, adds `POST /hg/api/music/skip` (`{"direction":"next"}` or `"prev"`), and `play_music` with `"dj": true`.
  4. Hermes stackchan MCP server: `robot_music_skip`, `robot_dj_mode`, `robot_play_music(dj=...)`.
  5. Robot tool behind it: `self.dj.mode` with `action` on, off, status, track, rave_on, rave_off.
- **Set list format** (`dj-setlist/1`), one file per set in `data/setlists/`:
  ```json
  {
    "title": "My first set",
    "format": "dj-setlist/1",
    "defaults": { "face": "happy", "lights": "party", "groove": true, "gap": 2, "vu": { "on": true, "color": "blue" } },
    "loop": false,
    "shuffle": false,
    "songs": [
      { "id": "opener", "track": "Song-file-name-without-mp3",
        "intro": [ { "speaker": "NOUS", "dialogue": "DJ Nous-Girl in the house!", "lang": "en" } ],
        "cues": [ { "at": 0, "action": "vu", "value": "purple" },
                  { "at": 30, "action": "face", "value": "scowl", "duration": 2 } ] }
    ]
  }
  ```
  Cue `action` can be face, lights, dance, head, pause, voice or vu; `at` is seconds into the song.
- **Turn it off:** "DJ mode off", or the Settings switch. Undo: `./install-addon.sh dj-mode rollback` (removes the example set; the DJ code stays in patched-server).

## Rave mode
- **What it does:** a full-screen color wash over her face that changes with the music, with the equalizer and LEDs on top. Colors are soft on purpose (at most 3 pulses a second, no red strobe). Meant for grown-ups at a party, not for kids.
- **Needs:** firmware v3.6.7-dj5 or later, DJ mode set up, the [rave-mode](../server/addons/rave-mode/) add-on (patched server + SOUL.md lines).
- **Turn it on:** voice only: "rave mode", "let's rave", "start a rave with Groove Salad". Not saved: off after a restart.
- **Use it:** "rave mode off" (music keeps going), "stop the rave" (music stops too). Next/previous work as usual.
- **Set it up yourself:** add `robot_rave_mode` to the stackchan MCP server (calls `self.dj.mode` with `rave_on` / `rave_off`; it turns DJ mode on too), and add the "Rave mode" section to SOUL.md so she keeps replies short and never offers faster flashing.
- **Turn it off:** "rave mode off". Undo: remove the rave lines from SOUL.md ([rave-mode](../server/addons/rave-mode/)).

## Internet radio
- **What it does:** plays listener-supported internet stations (SomaFM stations, Radio Paradise, Radio Swiss Jazz and Classic, and others). If a station doesn't answer in 8 s she says so and moves on.
- **Needs:** **your own patched server** ([patched-server](../server/addons/patched-server/) and [radio](../server/addons/radio/) add-ons), firmware v3.6.7-dj3 or later.
- **Use it:** "play the radio", "play Groove Salad", "put on some jazz", "next station", "what stations do you have", "stop the radio". Tap or talk to pause, "continue" to resume. Swipes change station.
- **Set it up yourself:** stations live in the server's `data/radio/stations.json` (`radio-stations/1` format: `default`, then `stations` with `id`, `name`, `aliases`, `url`, `genre`). The server streams with ffmpeg through her speech channel. Hermes gets `robot_radio` (play, next, previous, stop, list, status, resume) via the bridge's `POST /hg/api/radio`.
- **Good to know:** listening only, no kid filter on stations. Please support SomaFM if you use it.
- **Turn it off:** "stop the radio". Undo: `./install-addon.sh radio rollback`.

## Multilingual voice
- **What it does:** she answers in English, Spanish, French, German or Portuguese, each with its own voice, picking the language per sentence. Short unclear lines ("Okay!") keep the last language.
- **Needs:** **your own server** with the Phonon models.
- **Use it:** just talk to her in one of those languages.
- **Set it up yourself:**
  1. Download the models (table in the [README](../README.md#models)). Phonon 40M multilingual is gated: accept its terms on Hugging Face. It's non-commercial (CC-BY-NC-4.0).
  2. Run one Phonon TTS server per language with [ptts-openai-server](https://github.com/gradium-ai/xn-ptts), each with its voice file, plus the English Phonon 31M server as the fallback.
  3. The [multilingual](../server/addons/multilingual/) add-on adds a TTS provider to the voice server that guesses each sentence's language and sends it to that language's server, falling back to English 40M, then English 31M.
  4. Restart the voice server.
- **Good to know:** the on-robot language (Settings > Language) stays English. This is only her speaking voice.
- **Turn it off:** point the server's `TTS` back to a single voice. Undo: `./install-addon.sh multilingual rollback`.

## Camera and "what do you see"
- **What it does:** she takes a photo and a vision model on your server describes it. Optional face ID ("who is here?") for people you enrolled.
- **Turn it on:** Settings > **Camera & SD** > Camera (on by default).
- **Use it:** "what do you see?", "what am I holding?", "who is here?".
- **Set it up yourself:** the robot tool `self.camera.take_photo` uploads the photo to the server's vision endpoint, which asks a vision model (the maintainer uses Ollama `gemma4:e4b`). With Hermes, include `robot_what_do_you_see` (one photo per 5 s at most) and, for face ID, `robot_who_is_here`. Photos stay between the robot and your server.
- **Turn it off:** Settings > Camera & SD > Camera, off (no photos and no face follow).

## Follow faces
- **What it does:** her head follows the nearest face using on-robot face detection (esp-dl), and drifts home when nobody is there.
- **Turn it on:** Settings > Head > **Follow faces (camera)**. Camera must be on.
- **Use it:** "follow me", "stop following" (needs the follow tool in Hermes).
- **Set it up yourself:** add `robot_follow_faces` to the stackchan MCP server (calls `self.camera.follow_faces`). Firmware v3.6.3 or later.
- **Turn it off:** the same switch, or "stop following".

## Hermes Agent brain and memory
- **What it does:** gives her long-term memory, tools (robot body, email, memes, web) and her personality file.
- **Needs:** your own voice server and [Hermes Agent](https://hermes-agent.nousresearch.com/) on a computer it can reach.
- **Turn it on / set it up:** full steps in [HERMES.md](./HERMES.md): a separate `nous-girl` profile, its API server on localhost port 8650, the voice server's LLM pointed at it, then the stackchan MCP server.
- **Use it:** "remember that I like sharks", "what did we talk about yesterday?".
- **Turn it off:** point the voice server's `LLM` back at a plain model (setup C in the README).

## SOUL.md personality
- **What it does:** a Markdown file Hermes reads every conversation: who she is, how she talks, her hard limits, and how to use each tool.
- **Set it up yourself:** [HERMES.md](./HERMES.md#soulmd-her-personality-and-rules) has where it lives, a starter file and tips. Each feature above that has voice phrases has a matching SOUL.md section.
- **Turn it off:** you can't run Hermes without one, but you can edit it any time, then `hermes -p nous-girl gateway restart`.

## Kid-safe by default
- **What it does:** her normal mode is kid-safe: simple words, no scary, violent or grown-up content, no personal info, no secrets from parents, and "ask a grown-up" when unsure.
- **Where it comes from:** there is **no kid switch on the robot**. Kid safety lives in two places:
  1. **SOUL.md rules** (the "Talking with kids" and "Hard limits" sections, see [HERMES.md](./HERMES.md#soulmd-her-personality-and-rules)). These guide the model.
  2. **Hard checks in code**, which don't depend on the model: the meme maker refuses swearing, violence, drugs, politicians and named family members; email only goes to an allow-list after a read-back; the robot tools cap head angles and light brightness; rave colors are capped for photosensitive safety.
- **Set it up yourself:** start from the SOUL.md in [HERMES.md](./HERMES.md), keep the kids section, and use a model with good safety behavior. Grown-up features (rave mode, internet radio) are voice-only; leave their add-ons and SOUL.md lines out if kids use the robot alone.

## AgentMail email
- **What it does:** she has her **own** email inbox through [AgentMail](https://www.agentmail.to/), so she can send and read mail for you. She reads the address and message back and waits for your yes.
- **Needs:** Hermes, an AgentMail account of your own.
- **Use it:** "send an email to ...", "check your email", "any new mail?".
- **Set it up yourself:**
  1. Create an inbox in your AgentMail account and make an API key.
  2. Put the key in her **profile `.env`**, never in config or git:
     ```
     AGENTMAIL_API_KEY=<your-key>
     ```
  3. Add AgentMail's hosted MCP server to the profile's `config.yaml`:
     ```yaml
     mcp_servers:
       agentmail:
         url: https://mcp.agentmail.to/mcp
         headers:
           x-api-key: ${AGENTMAIL_API_KEY}
         enabled: true
         timeout: 60
         tools:
           include: [list_inboxes, get_inbox, list_threads, get_thread, list_messages, get_message,
                     send_message, reply_to_message, forward_message, get_attachment]
     ```
  4. Add email rules to SOUL.md: always read back the address and message, send only after a clear yes, never email strangers, and never send keys or private files.
  5. `hermes -p nous-girl gateway restart`.
- **Turn it off:** set `enabled: false` (or remove the block) and restart the gateway.

## Local meme maker
- **What it does:** she makes classic two-line memes on your computer with Pillow (about 60 classic templates). No cloud, no keys, no AI image model. The render takes well under a second; the whole thing takes about 5 to 10 s with her thinking and talking.
- **Needs:** Hermes, the [local-memes](../server/addons/local-memes/) add-on.
- **Use it:** "make a meme about homework", "show me the latest meme", "show me the homework meme".
- **Set it up yourself:** the add-on adds a small stdio MCP server, `nousgirl_localmeme`, with two tools: `list_templates(query)` and `make_meme(template, top_text, bottom_text, lines, title)`. A script downloads the blank template pictures to your computer (they are copyrighted stills, so they are not in this repo). Each meme is saved to `~/NousGirl-memes` as the original plus 320x240 JPG and PNG copies. A word filter refuses unkind content. SOUL.md gets a "Making memes" section.
- **Turn it off:** `./install-addon.sh local-memes rollback`, and remove its block from the Hermes config.

## Meme previews on her screen
- **What it does:** shows a saved meme on her screen. **Tap to hide it.**
- **Needs:** firmware **dj7** (dj6 can reboot while showing a preview), **your own patched server** ([patched-server](../server/addons/patched-server/) and [meme-previews](../server/addons/meme-previews/)), the local meme maker.
- **Use it:** "show me the latest meme", "show me the homework meme".
- **Set it up yourself:**
  1. Hermes tool `robot_show_meme(name)` makes a **160x120** opaque PNG in memory from the saved meme. 160 pixels wide draws at the same size as 320x240 on her screen, with a quarter of the memory.
  2. It sends it to the bridge's `POST /hg/api/show_meme`, which refuses anything bigger than 160x120 or **64 KB**, then tells the robot to fetch it with `self.screen.preview_image`.
  3. The dj7 firmware downloads previews into PSRAM, needs a `Content-Length` and refuses anything over 256 KB.
- **Turn it off:** remove `robot_show_meme` from the Hermes tools and the meme-preview lines from SOUL.md ([meme-previews](../server/addons/meme-previews/)).

## Email this meme (email_meme)
- **What it does:** emails a saved meme as a JPEG from her AgentMail inbox, **only to addresses you approved**.
- **Needs:** AgentMail email and the local meme maker, plus the [email-meme](../server/addons/email-meme/) add-on.
- **Use it:** "email me that meme", "email the homework meme to dad". She reads it back: "I'll email the meme ... to ... Should I send it?" She sends only after a clear yes.
- **Set it up yourself:** a separate small MCP server, `nousgirl_emailmeme`, with one tool, `email_meme(name, to, note, confirm)`:
  - The first call (no confirm) sends nothing; it returns the address and title for her to read back.
  - A send only happens with `confirm=true` matching a read-back from the last 5 minutes, once.
  - Recipients come only from an allow-list file on your computer, **outside the repo**, private permissions, one per line, optionally named: `me = <address>`.
  - At most 10 sends an hour. Every send is logged next to the memes. The AgentMail key is read from the profile `.env` at send time and never logged.
- **Turn it off:** `./install-addon.sh email-meme rollback`, and remove its block from the Hermes config (your allow-list and the log stay).

## Actor Mode
- **What it does:** she performs a script you wrote: lines, faces, lights, dances and pauses, in order.
- **Needs:** **your own patched server** ([patched-server](../server/addons/patched-server/); example scripts in [actor-mode](../server/addons/actor-mode/)).
- **Use it:** "action mode two" (runs script 2), or ask which scripts she has.
- **Set it up yourself:** put scripts in the server's `data/scenes/` folder. Full format, every cue and a first-script walkthrough: [ACTOR-MODE.md](./ACTOR-MODE.md). Check a script offline with [validate_scene.py](../examples/actor-mode/validate_scene.py); a working example is [hello-show.json](../examples/actor-mode/hello-show.json).
- **Turn it off:** `plugins.scene_mode.enabled: false` in `data/.config.yaml`, then restart. `./install-addon.sh actor-mode rollback` removes the example scripts.

## Fun pack: games, stories and kid memory
- **What it does:** trivia, spelling bee and memory games with patient pacing, choose-your-own story time, and kid-friendly memory rules ("remember I like sharks").
- **Needs:** Hermes, the [fun-pack](../server/addons/fun-pack/) add-on (Hermes skills plus a SOUL.md section).
- **Use it:** "let's play trivia about space", "spelling bee", "tell me a story about a dragon", "remember I like sharks", "forget that I like sharks". "Stop" ends a game.
- **Turn it off:** `./install-addon.sh fun-pack rollback`, remove its SOUL.md section, then restart the gateway.

## Sleep and power
- **What it does:** on battery, the screen dims after about 5 minutes (300 s) with nothing happening, and comes back when you talk or tap. On USB power it doesn't dim. She never turns herself off on a timer.
- **Turn it on:** automatic, no setting. Details: [SETTINGS.md](./SETTINGS.md#sleep-and-power-saving).

## Crash report
- **What it does:** after any unexpected restart, Settings > **About** shows why ("Started because" and "Last crash"), with memory and battery at the time. The voice server log gets it as `hg_boot`.
- **Use it:** take a photo of the About page for bug reports.
- **Added in:** v3.6.6. dj7 also shows free PSRAM.

## Wi-Fi and phone hotspot
- **What it does:** keeps up to 10 networks, saves a phone hotspot even if the first test fails, and joins it when it shows up.
- **Use it:** Settings > **Wi-Fi** > **Wi-Fi setup mode** to add one ([steps](./SELF-HOSTING.md#step-2-put-the-robot-on-your-wi-fi)). 2.4 GHz only; on iPhone turn on Maximize Compatibility. Phone hotspot step by step, iPhone and Android: [HOTSPOT.md](./HOTSPOT.md).
- **Turn it off:** Factory reset forgets all networks.

## Flashing, updates and rollback
- **What it does:** puts a Nous-Girl build on the robot, updates it, or goes back to an older one.
- **Use it:** [VERSIONS.md](../VERSIONS.md#how-to-flash) has the exact commands: back up first, full install at `0x0` (wipes Wi-Fi), or update with app + assets (keeps Wi-Fi). Roll back with the same command and an older version, one step at a time. Flashing from Windows, Linux or a browser: [PLATFORMS.md](./PLATFORMS.md#flash-the-robot).
- **Updates from the server:** the upstream firmware asks its server address for a newer version when it starts, and installs one if the server offers it. Nous-Girl releases are **not** delivered that way; flash them over USB.

## IR remote (experimental)
- **What it does:** learns one button from an IR remote and sends it back ("learn a remote button", then "send it").
- **Status:** v3.6.6-ir side build only. Never tested on hardware, not in the dj line, not released.
