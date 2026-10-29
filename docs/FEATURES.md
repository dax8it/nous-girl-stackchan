# Nous-Girl features, in plain English

One section per feature: what it does, how to use it, which version added it, how to set it up, and how to undo it. Firmware versions are in [VERSIONS.md](../VERSIONS.md). "Pack" means a server or Hermes add-on with `dry-run`, `apply` and `rollback` commands (the packs are not in this repo yet, see the README).

Voice examples assume you wake her first with "Hey Nous-Girl" (or "Hey Jarvis").

---

## Wake words
- **What it does:** she listens for "Hey Nous-Girl" (a custom phrase) and "Hey Jarvis".
- **How to use:** say the wake word, then talk. Tap the screen to stop her while she talks.
- **Added in:** v3. v3.3 made "Hey Nous-Girl" run only while she's idle, so it no longer starves her audio.
- **Setup:** Settings > Wake words: turn each word on or off, pick sensitivity, then **Restart to apply**.
- **Good to know:** the custom phrase keeps one CPU core busy. On dj7 you may still see occasional choppy audio; turning off "Hey Nous-Girl" fixes that if it bothers you. "Interrupt with wake word" was tried in v3.6.5 and compiled out in v3.6.6 (no echo cancellation on this board).
- **Undo:** turn it off in Settings. No flash needed.

## Faces, lip sync and moods
- **What it does:** animated faces (public builds: the CC0 robot, boy or girl starter face, or [your own](./SPRITES.md)) that follow her state (listening, thinking, speaking, sleeping), a mouth that moves with her voice, and moods: happy, sad, thinking, pet (loving), shake (dizzy), look left/right, email, scowl.
- **How to use:** automatic. Her brain can also set a face ("look happy").
- **Added in:** v1 (faces), v3 (lip sync), v3.5 (vowel mouth shapes, talking in other moods, party face), v3.6.7-dj (scowl).
- **Setup:** Settings > Screen: lip sync on/off, vowel mouth shapes, talking lights. The power key switches face style (drawn face or classic line face) in v3.6+.
- **Undo:** the same switches.

## Lights
- **What it does:** 12 RGB LEDs: colors, moods, rainbow, brightness that follows her voice (talking lights), and beat-synced colors in DJ mode.
- **How to use:** "turn your lights blue", "rainbow lights", "lights off".
- **Added in:** v1; talking lights v3.5; beat sync v3.6.7-dj3.
- **Setup:** Settings > Lights. Voice control needs the stackchan robot tools in Hermes (the addonly pack).
- **Undo:** Settings > Lights > Auto.

## Head touch and moves
- **What it does:** 3-pad head sensor: tap front, middle or back, hold, swipe. Swipe forward = next song or dance, swipe back = stop. She looks around when idle and shakes her head if you jolt her.
- **Added in:** v3.5 (real 3-pad touch), v3.4 (shake reaction ends properly).
- **Setup:** Settings > Head: level up/down, go home, idle motion, "Swap head swipe direction".

## Dance
- **What it does:** preset dances (wiggle, nod, disco, robot), dances you record yourself, and grooving to music.
- **How to use:** "do a dance", "do the robot dance", "dance to this". Record one in Settings > Dance > Learn a dance.
- **Added in:** v1 (dances and recorder), v3 (Settings page), v3.6.7-dj (groove loop in DJ mode).
- **Setup:** Settings > Dance and Settings > Music > "Dance to music". Voice control needs the robot tools in Hermes.

## Music
- **What it does:** plays songs from the computer's music folder (through the voice server) or from a microSD card.
- **How to use:** "play music", "play <song name>", "next song", "previous song", "stop". Swipe the screen sideways while a song plays: right-to-left = next, left-to-right = previous.
- **Added in:** v1 (music), v3.6.7-dj (real next/previous, screen swipes), dj5 (reliable swipes).
- **Setup:** put songs in the server's music folder, or on a FAT32 microSD card (32 GB or smaller).

## DJ mode
- **What it does:** she DJs your music: intros, set lists with timed face/light/dance cues, a 13-band equalizer on screen with her face, colors that cycle and flash on the beat, LEDs to match, and head grooving.
- **How to use:** "DJ mode" or "be my DJ" (she lists set lists and waits for a pick), "DJ mode 1", "DJ mode off", "stop DJ". Tap to pause a set, "continue" to resume.
- **Added in:** firmware v3.6.7-dj (dj2 equalizer, dj3 colors, dj5 swipe fix), plus the **dj** server pack.
- **Setup:** flash dj5 or later, then the dj pack: `./apply-dj.sh dry-run`, then `./apply-dj.sh apply`. Set lists are JSON files in the server's `data/setlists/` (`dj-setlist/1` format). Settings > Music > "DJ mode" switch.
- **Undo:** `./apply-dj.sh rollback`. Turn the Settings switch off.

## Rave mode
- **What it does:** a full-screen color washes over her face (like a duotone poster) and changes with the music, with the EQ and LEDs on top. Flash speed and brightness are capped for photosensitive safety. Meant for grown-ups at a party, not for kids.
- **How to use:** "rave mode", "let's rave", "start a rave with Groove Salad", "rave mode off" (music keeps going), "stop the rave" (music stops too).
- **Added in:** firmware v3.6.7-dj4 (dj5 made it voice-only), plus the **rave** pack.
- **Setup:** flash dj5 or later, apply the dj pack (and radio, if you want it) first, then `./apply-rave.sh dry-run` and `./apply-rave.sh apply`.
- **Undo:** `./apply-rave.sh rollback`.

## Internet radio
- **What it does:** plays listener-supported internet stations (SomaFM stations, Radio Swiss Jazz and Classic, and others). If a station doesn't answer in 8 s she says so and moves on.
- **How to use:** "play the radio", "play Groove Salad", "put on some jazz", "next station", "what stations do you have", "stop the radio". Tap or talk to pause, "continue" to resume.
- **Added in:** **radio** pack (works with v3.6.7-dj3 and later).
- **Setup:** `./apply-radio.sh dry-run`, `./apply-radio.sh apply`. `./apply-radio.sh check-streams` re-tests every station.
- **Undo:** `./apply-radio.sh rollback`.
- **Good to know:** listening only. Please support SomaFM if you use it.

## Multilingual voice
- **What it does:** she answers in English, Spanish, French, German or Portuguese, each with its own voice, using the Phonon 40M multilingual model. She picks the language per sentence; short unclear lines keep the last language. English Phonon 31M is always running as a fallback.
- **How to use:** just talk to her in one of those languages.
- **Added in:** **multilingual** server pack (no firmware change).
- **Setup:** download the models (see the models table in the README), then `./apply-multilingual.sh apply --dry-run` and `./apply-multilingual.sh apply`. It checks each voice server and switches back by itself on failure.
- **Undo:** `./apply-multilingual.sh rollback`.
- **Good to know:** Phonon 40M multilingual is gated and non-commercial (CC-BY-NC-4.0).

## Camera, photos and "what do you see"
- **What it does:** she takes a photo and a vision model describes it. Optional face ID ("who is here") for people you enrolled.
- **How to use:** "what do you see?", "take a photo", "who is here?".
- **Added in:** v1 (camera), v3.6.2 to v3.6.4 (reliable photos and face ID).
- **Setup:** Settings > Camera on. The vision model runs on the server (we use Ollama `gemma4:e4b`). Voice control needs the robot tools in Hermes.
- **Undo:** Settings > Camera off (no photos, no face follow).

## Follow faces
- **What it does:** her head follows the nearest face using on-robot face detection, and drifts back home when nobody is there.
- **How to use:** "follow me", "stop following". Or Settings > Head > Follow faces.
- **Added in:** v3.5 (skin color, removed), v3.6 (real face detection), v3.6.3 (works reliably, voice switch), plus the **follow-tool** Hermes pack for the voice command.
- **Setup:** flash v3.6.3 or later, then `./apply-follow-tool.sh dry-run` and `./apply-follow-tool.sh apply`.
- **Undo:** `./apply-follow-tool.sh rollback`.

## Actor Mode
- **What it does:** she performs a script you wrote: lines, faces, lights, dances and pauses, in order.
- **How to use:** "action mode two" (runs script 2), or ask which scripts she has.
- **Added in:** **scene**, **actor** and **actor-json** server packs (JSON scripts with timed cues).
- **Setup:** put scripts in the server's `data/scenes/` folder, then `./apply-actor.sh apply` (and `./apply-actor-json.sh apply` for JSON scripts).
- **Undo:** `./apply-actor.sh rollback <timestamp>`.

## Fun pack: games, stories and kid memory
- **What it does:** trivia, spelling bee and memory games with patient pacing, choose-your-own story time, and kid-friendly memory rules ("remember I like sharks").
- **How to use:** "let's play trivia about space", "spelling bee", "tell me a story about a dragon", "remember I like sharks", "forget that I like sharks". Say "stop" to end.
- **Added in:** **fun-pack v2** Hermes pack (no firmware change).
- **Setup:** `./apply-fun-pack.sh dry-run`, `./apply-fun-pack.sh apply`, then restart her Hermes gateway.
- **Undo:** `./apply-fun-pack.sh rollback`, then restart the gateway.

## AgentMail email
- **What it does:** she has her own email inbox through [AgentMail](https://www.agentmail.to/), so she can send and read mail for you.
- **How to use:** "send an email to ...", "check your email". She reads the address and message back and waits for your yes before sending.
- **Added in:** **agentmail** Hermes pack.
- **Setup:** create an AgentMail inbox, put the key in her Hermes profile `.env` as `AGENTMAIL_API_KEY` (never in config or git), then `./apply-agentmail.sh dry-run` and `./apply-agentmail.sh apply`. Her SOUL.md gets email rules (read back, confirm, never email strangers).
- **Undo:** `./apply-agentmail.sh rollback`.

## Local meme maker
- **What it does:** she makes classic two-line memes on your computer with Pillow (about 60 classic templates). No cloud, no keys, no AI image model. The render takes well under a second; the whole thing takes about 5 to 10 s including her thinking and talking. Each meme is saved to `~/NousGirl-memes` as the original plus 320x240 JPG and PNG copies.
- **How to use:** "make a meme about homework", "show me the latest meme", "show me the homework meme".
- **Added in:** **local-memes** pack (replaced the earlier cloud meme service), **meme-screen** pack (shows it on her screen), **small-preview** pack (160x120 previews).
- **Setup:** `./apply-local-memes.sh dry-run`, `./apply-local-memes.sh apply`, restart her gateway. Template images are downloaded to your computer by a script; they are not in this repo (they are copyrighted stills).
- **Showing memes:** the preview is sent at 160x120, which draws at the same size as 320x240 on her screen with a quarter of the memory. Use firmware **dj7**; dj6 can reboot while showing a preview.
- **Undo:** `./apply-local-memes.sh rollback` (and `./apply-small-preview.sh rollback` for the preview size).

## Email this meme (email_meme)
- **What it does:** emails a saved meme as a JPEG attachment from her inbox, **only to addresses you approved**.
- **How to use:** "email me that meme", "email the homework meme to dad". She reads it back: "I'll email the meme ... to ... Should I send it?" She sends only after a clear yes.
- **Safety:** recipients come only from an allow-list file on your computer (outside the repo, private permissions). You can name entries, one per line, like `me = <address>` and `dad = <address>`. Anything not on the list is refused, even if confirmed. A yes is good for one send within 5 minutes. At most 10 sends an hour. Every send is logged next to the memes.
- **Added in:** **email-meme** Hermes pack (needs agentmail and local-memes).
- **Setup:** `./apply-email-meme.sh dry-run`, `./apply-email-meme.sh apply`, add addresses to the allow-list, `./apply-email-meme.sh status`, restart her gateway.
- **Undo:** `./apply-email-meme.sh rollback` (keeps your allow-list and the log).

## Screen text cleanup
- **What it does:** removes odd punctuation from the text on her screen without changing how she speaks.
- **Added in:** **punct** server pack. **Undo:** `./apply-punct.sh rollback`.

## Crash report
- **What it does:** after any unexpected restart, Settings > About shows why ("Started because" and "Last crash"), with memory and battery at the time. The voice server log gets it as `hg_boot`.
- **Added in:** v3.6.6. dj7 also logs free PSRAM.

## Wi-Fi and phone hotspot
- **What it does:** keeps up to 10 networks, saves a phone hotspot even if the first test fails, and joins it when it shows up. Settings > Wi-Fi lists saved names (never passwords).
- **Added in:** v2 (US channels), v3.6.5 (hotspot save and auto-join), v3.6.6 (no scanning while connected).

## IR remote (experimental)
- **What it does:** learns one button from an IR remote and sends it back.
- **How to use:** "learn a remote button", then "send it".
- **Added in:** v3.6.6-ir side build only. Never tested on hardware, not in the dj line.
