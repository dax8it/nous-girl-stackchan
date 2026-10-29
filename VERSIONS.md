# Nous-Girl firmware versions and checkpoints

Every firmware build, oldest to newest, with what it added, what's known to be wrong with it, its SHA-256, and how to flash it or roll back to it. If you want to start from an earlier version, pick a row and use its flash command.

> **Current recommended: `v3.6.7-dj7`** (Oct 28, 2026). Confirmed working on the maintainer's robot.
> **Safe fallback: `v3.6.7-dj5`.** Avoid `v3.6.7-dj6`: it can reboot while showing a meme preview.

All times are US Eastern. All builds target the **M5Stack StackChan K151 (CoreS3, ESP32-S3, 16 MB flash)**, built with ESP-IDF 6.1.0 (see [docs/TOOLCHAIN-VERSIONS.md](./docs/TOOLCHAIN-VERSIONS.md)). Upstream bases: [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) and fairy-stackchan.

## Where the binaries are

Binaries are **not committed to git**. Each version is a [GitHub Release](https://github.com/dax8it/The-one-above-all-stackchan/releases) with its `.bin` attached (see [docs/RELEASING.md](./docs/RELEASING.md)). Checksums for every file are in [firmware/SHA256SUMS](./firmware/SHA256SUMS). Check before you flash:

```
shasum -a 256 -c SHA256SUMS --ignore-missing
```

## Quick table

| Version | Date | Highlights | Layout | Status |
|---|---|---|---|---|
| v1 | Oct 22 | First headphone-girl build: GIF faces, lights, music, dances | old | superseded |
| v2 | Oct 22 | English, GIF faces from boot, US Wi-Fi channels | old | superseded |
| v3 | Oct 22 | Settings screen, lip sync, wake words, head level | old | superseded |
| v3.1 | Oct 22 | Settings taps fixed | old | superseded |
| v3.2 | Oct 22 | v3.1 + "Home server" preset | old | superseded |
| v3.3 | Oct 22 | Fixes no sound / dragging speech | old | superseded |
| v3.4 | Oct 22 | Shake reaction ends properly | old | **last old-layout checkpoint** |
| v3.5 | Oct 23 | Vowel lip sync, party face, head touch, talking lights | old | checkpoint |
| v3.6 | Oct 23 | On-device face detection, **new partition table** | new | **first new-layout checkpoint (full flash)** |
| v3.6.1 | Oct 23 | Memory fix for connect errors, level head | new | superseded |
| v3.6.2 | Oct 23 | Photo / face ID reliability | new | superseded |
| v3.6.3 | Oct 23 | Follow faces actually runs, voice switch | new | superseded |
| v3.6.4 | Oct 23 | Photo fixes, step-by-step photo logs | new | superseded |
| v3.6.5 | Oct 23 | Battery power-on fix, phone hotspot setup | new | superseded |
| v3.6.6 | Oct 23 | Crash report at boot, no Wi-Fi scan while connected | new | **pre-DJ checkpoint** |
| v3.6.6-ir | Oct 24 | Side build: IR remote learn/send | new | experimental, never run on hardware |
| v3.6.7-dj | Oct 27 | DJ mode, set lists, swipe next/prev, scowl face | new | superseded |
| v3.6.7-dj2 | Oct 27 | 13-band equalizer screen | new | superseded |
| v3.6.7-dj3 | Oct 27 | Color cycling EQ, beat flash, LEDs match | new | superseded |
| v3.6.7-dj4 | Oct 27 | Rave mode | new | superseded |
| v3.6.7-dj5 | Oct 27 | Reliable swipes, rave by voice only | new | **fallback** |
| v3.6.7-dj6 | Oct 28 | Full-screen meme tool | new | **known crash, avoid** |
| v3.6.7-dj7 | Oct 28 | Meme preview crash fixed, memory freed | new | **current recommended** |

"Layout" is the flash partition table. **old** = app slots at 0x20000/0x410000, assets at 0x800000. **new** (`16m_hg36`, from v3.6) = two 5.4375 MB app slots at 0x20000/0x590000, assets 5 MB at 0xB00000. Moving between old and new needs one full flash (see [Moving between layouts](#moving-between-layouts)).

## How to flash

Close any serial monitor first. Opening the port resets the robot. Replace `PORT` with your port (`ls /dev/cu.usbmodem*` on a Mac). Install esptool with `pip install esptool` (or `brew install esptool`).

**Always back up first** (read only, about 2 minutes):

```
esptool --chip esp32s3 -p PORT -b 460800 read-flash 0x0 0x1000000 my-stackchan-backup.bin
```

Keep that file private: it holds your Wi-Fi and settings.

**App-only flash** (keeps Wi-Fi and settings). Works between any two versions on the same layout:

```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x20000 xiaozhi-v3.6.7-dj7.bin
```

Swap the file name for any other version. Settings > About shows the version afterwards.

**Rolling back** is the same command with the older file. Roll back one step at a time, and only when something is broken. Don't run several rollback commands in a row: the last one wins.

**Restore your backup** (exactly what you had before):

```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 my-stackchan-backup.bin
```

### Moving between layouts

- **Old to new (anything up to v3.5, or stock firmware, to v3.6 or later):** do one full flash of `headphone-girl-merged-0x0-v3.6.bin` at `0x0`, then app-only flash the version you want. The full flash wipes Wi-Fi, so you redo hotspot setup.

  ```
  esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after no-reset erase-flash
  ```
  ```
  esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 headphone-girl-merged-0x0-v3.6.bin
  ```
  ```
  esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x20000 xiaozhi-v3.6.7-dj7.bin
  ```
- **New back to old (v3.6+ to v3.4 or earlier):** full flash `headphone-girl-merged-0x0-v3.4.bin` at `0x0` (same erase + write commands, different file). For v3.5, flash v3.4 full first, then `xiaozhi-v3.5.bin` at `0x20000`.
- v3 and later on the old layout also need the assets (faces and wake-word models) at `0x800000`. The merged images already include them.

## Version notes

### v1, v2, v3 (Oct 22): the first builds
- **v1** `headphone-girl-merged-0x0.bin`, full flash at 0x0. Headphone-girl GIF faces that follow her state, 12 RGB lights, music and dancing, joystick dance recorder, microSD support, camera.
- **v2** `headphone-girl-merged-0x0-v2.bin`, full flash. English build, GIF faces from boot (not only after Wi-Fi), `gear` face mapped, Wi-Fi country US (channels 1 to 11) so the setup hotspot stays visible, ESP-NOW fix.
- **v3** `headphone-girl-merged-0x0-v3.bin`, full flash. Settings screen (hold the screen 1.5 s), loudness-based lip sync, wake words "Hey Nous-Girl" (custom MultiNet phrase) and "Hey Jarvis", head level fixed at 3 degrees, dance presets, music page, SD card layout.
- Known issue in v3: Settings taps didn't register (fixed in v3.1).

### v3.1 to v3.4 (Oct 22)
- **v3.1:** Settings taps work. LVGL moved to core 0, the audio front end pinned to core 1, Settings closes after 60 s idle, and alerts can't cover it.
- **v3.2:** v3.1 plus a "Home server" preset on the Server page. The preset points at the maintainer's own server; type your own OTA URL instead.
- **v3.3:** fixed no sound and slow, repeating speech. Cause: the "Hey Nous-Girl" detector kept core 1 at 100%. It now runs only while she's idle and only around detected speech. The opus decoder got a higher priority on core 0.
- **v3.4:** the shake (dizzy) face now ends 1.5 s after the last real jolt; her own head moves no longer count as shakes.

### v3.5 (Oct 23): personality
Vowel lip sync (a, i, u, e, o) with 8 mouth frames. Party face during music and dances (notes, rainbow lights on the beat). The mouth moves on sad, thinking and loving faces too. Follow faces (first version, skin color). Real 3-pad head touch: tap, hold, swipe forward = next song or dance, swipe back = stop. Talking lights follow her voice. App-only on the old layout.

### v3.6 (Oct 23): face detection and new layout
On-device face detection (Espressif esp-dl, MIT) replaces skin-color tracking. Power key short press switches face style only. **New partition table: full flash once** (files: bootloader, partition table, otadata, app, assets, or the merged `headphone-girl-merged-0x0-v3.6.bin`).

### v3.6.1 to v3.6.6 (Oct 23)
- **v3.6.1:** fixed TLS / connect errors caused by internal RAM running out (esp-dl moved to PSRAM). Level pitch back to 3. Voice page simplified.
- **v3.6.2:** photo and face ID: a camera gate pauses follow faces while a photo is taken and encoded.
- **v3.6.3:** follow faces never actually ran on v3.6.2 (not enough internal RAM); fixed. Voice switch for follow faces.
- **v3.6.4:** more take_photo fixes and step-by-step photo log lines.
- **v3.6.5:** battery power-on soft-start fix. Phone-only hotspot setup saves the network even when the test connect fails, and joins it later. "Interrupt with wake word" option (off by default).
- **v3.6.6:** wake-word barge-in compiled off (no echo cancellation on CoreS3, so it couldn't work). No Wi-Fi scan while connected. **Boot crash report:** Settings > About shows why she last restarted, and every connect sends it to the server as `hg_boot`.
- Known issue up to v3.6.6: rare reboot during very long replies (crash report added to track it).

### v3.6.6-ir (Oct 24, side build)
v3.6.6 plus the built-in IR receiver and transmitter: tools `self.ir.learn`, `self.ir.send_last`, `self.ir.status`. Built and compiled only, **never run on hardware**. Not part of the main line.

### v3.6.7-dj series (Oct 27 to 28): DJ, rave, memes
- **dj:** DJ mode (Settings > Music, or say "DJ mode"), set lists, screen swipe next/previous, groove dancing, new scowl face.
- **dj2:** cleaner DJ screen: just her face plus a 13-band equalizer computed on the robot from the music she plays.
- **dj3:** every EQ column its own color, palette turns and flashes on the beat, her 12 LEDs match.
- **dj4:** rave mode: full-screen color over her face, punchier colors, with a photosensitive flash-safety cap.
- **dj5:** swipes were often read as taps (touch read dropouts on a busy I2C bus); a new touch tracker fixes it. Rave is voice-only. **Fallback version.**
- **dj6:** new full-screen meme tool `self.screen.show_image`. **Known issue:** she can reboot while showing a 320x240 meme preview (the download used scarce internal RAM). Avoid.
- **dj7:** fixes the dj6 crash. The preview download goes to PSRAM, the full-screen tool is removed, the LVGL memory pool and image cache are smaller (1.5 MB more free PSRAM), old previews are cleared from memory, and PSRAM is logged. "Hey Nous-Girl" unchanged. Best with the small-preview server pack (160x120 previews). **Current recommended.**
- Known issues on dj7: "AFE(FEED) ringbuffer full" and task-watchdog warnings remain while the custom wake word runs (it keeps core 1 busy), which can make her voice choppy at times.

## Checksums (SHA-256)

| Version | File | SHA-256 |
|---|---|---|
| v1 | headphone-girl-merged-0x0.bin | `09256623232e2d0c4c0752ffae4eb59610fdacb364301c42fcf39dbaf71b94e1` |
| v2 | headphone-girl-merged-0x0-v2.bin | `d2c5b13ad87458e2835965656e09746d8902455064462a8db1a77230f4d4ff9b` |
| v3 | headphone-girl-merged-0x0-v3.bin | `9596811ce338e386cfdd2b1f91da1ec0a31c4068f7f6a6f805b312a2135823f7` |
| v3.1 | headphone-girl-merged-0x0-v3.1.bin | `ae35bbb4a0431d456ed3de749f0a06bf091f2fd9735b2d7a8d5bc9678b3535de` |
| v3.1 | xiaozhi-v3.1.bin (app) | `393578e5b3106f31cfce4bb5f049f411a3bd9dac50731a2fc6251eb9da1bc90e` |
| v3.2 | headphone-girl-merged-0x0-v3.2.bin | `ae2b457ee08c6ec388b14648375b68402f2216f11eef0e1331e6d89fb23e7894` |
| v3.2 | xiaozhi-v3.2.bin (app) | `ca196569fcb5763eccf987699711b57604cf99de5496e7ee0a595ed849f9a3b0` |
| v3.3 | headphone-girl-merged-0x0-v3.3.bin | `8fb733b2623a7d0c6cfbc0f5dd1560a622be785672ced82128f7e3f55d5c067b` |
| v3.3 | xiaozhi-v3.3.bin (app) | `653d89526847049e38d6123a98e1e7b4a1525ad34ed2902d8151abdb50f4e32e` |
| v3.4 | headphone-girl-merged-0x0-v3.4.bin | `4749f83a397fadef288790ee35a063f7b6892898cb05242612152dd09f69551d` |
| v3.4 | xiaozhi-v3.4.bin (app) | `49026fe36fcabd050204f0304f604d68c528acb66df4433eb46443371b88c6cc` |
| v3.5 | xiaozhi-v3.5.bin (app, old layout) | `43ab6866d8ee307675135d5e4f327f78e883944dbdaf6c9d3b15b75ab8416a85` |
| v3.6 | headphone-girl-merged-0x0-v3.6.bin | `a591d6273fdb8286f31b34c6685701df1a71e4a432a969eaf85c234333fbe40a` |
| v3.6 | xiaozhi-v3.6.bin (app) | `06544d4271bf838879916dd57170e26c54f8f46d13bf7827dd4d9f9b9a36fe40` |
| v3.6 | assets-v3.6.bin (0xB00000) | `430a0920ec8564dd1d06d61a93d51eabe637fa315f691ffd1ef4048d715a2536` |
| v3.6.1 | xiaozhi-v3.6.1.bin | `cde1059ed7b9073e85602815622603a6592ca9e9e73c49956af89d5ec06d0e15` |
| v3.6.2 | xiaozhi-v3.6.2.bin | `0c04230d6788887bcbc583601a532c020a11c16724b92f322e44e11c02518a14` |
| v3.6.3 | xiaozhi-v3.6.3.bin | `4d390253ec071f75bdb7ddca629b76a7c18d01cd8af286afccaf47915284ed8d` |
| v3.6.4 | xiaozhi-v3.6.4.bin | `a66455457e4511f9e596e554005cad01588f85f61f17b0f9b99158efbd1761f0` |
| v3.6.5 | xiaozhi-v3.6.5.bin | `297ee9ee68242f4c8b7fb8da9e13cf155636f6ec9965b7755d8f65f59a1efc34` |
| v3.6.6 | xiaozhi-v3.6.6.bin | `973c2d9c3c8d66180714048dd174f818509c6b876994d1a35bdf738fca494678` |
| v3.6.6-ir | xiaozhi-v3.6.6-ir.bin | `cce39b27c54a36bdfedcea4212ce30e6562cab3fad71cebd6761057886bc3a8f` |
| v3.6.7-dj | xiaozhi-v3.6.7-dj.bin | `9f4751fbc324861d0eaaf410c2d07c1a1c51564dfd1cc8456811236e4cadaa1c` |
| v3.6.7-dj2 | xiaozhi-v3.6.7-dj2.bin | `3ce465b9565c3dedb4a436baf6e3b7dfa095c0aeceb5cb4992a642e4c9efdea2` |
| v3.6.7-dj3 | xiaozhi-v3.6.7-dj3.bin | `8318768942524ccca989334ceb02034659de268cae0cd3fb64a9e78c3714583c` |
| v3.6.7-dj4 | xiaozhi-v3.6.7-dj4.bin | `6048f09f160566a2f88da7e6e3f80860cba94d4cc7aaa422a1c6f67b738f7916` |
| v3.6.7-dj5 | xiaozhi-v3.6.7-dj5.bin | `766105b069b6901733725ffb02026c39eea2f41d81c1ff6d60f9adf24e15a89e` |
| v3.6.7-dj6 | xiaozhi-v3.6.7-dj6.bin | `887caee15817fecad4d4e7b13d66bdbed5339136d5b19b1ae690e8edcf34618c` |
| v3.6.7-dj7 | xiaozhi-v3.6.7-dj7.bin | `cd6fefa5d7551ef9a78dfbb81a649b7f2b86315ea97b1e37543b6b1ab628bc04` |

Building from source instead: the patched firmware source for each version is not public yet (see the open items in [docs/RELEASING.md](./docs/RELEASING.md)). The toolchain is in [docs/TOOLCHAIN-VERSIONS.md](./docs/TOOLCHAIN-VERSIONS.md).

## Server and Hermes packs

Many features live on the server or in the Hermes brain, not in the firmware. They were built as small "packs", each with its own script: `status` (read only), `dry-run` (shows changes, writes nothing), `apply` (backs up first), `rollback [TIMESTAMP]` (puts the backup back). These packs are **not in this repo yet**; they will be added after a secrets scrub. What each does is in [docs/FEATURES.md](./docs/FEATURES.md).

| Pack | Side | What it does | Needs firmware | Commands |
|---|---|---|---|---|
| multilingual | server | Phonon voices in en/es/fr/de/pt, picks the language per sentence | any | `./apply-multilingual.sh apply --dry-run` / `apply` / `rollback [STAMP]` |
| punct | server | cleans punctuation for her screen text | any | `./apply-punct.sh dry-run` / `apply` / `rollback [TS]` |
| scene, actor, actor-json | server | Actor Mode: scripted scenes with face, light and dance cues | any | `./apply-actor.sh apply` / `status` / `rollback <ts>` |
| dj | server + Hermes | DJ mode, set lists, real next/previous | v3.6.7-dj+ | `./apply-dj.sh dry-run` / `apply` / `rollback [TS]` |
| radio | server + Hermes | internet radio stations | v3.6.7-dj3+ | `./apply-radio.sh dry-run` / `apply` / `check-streams` / `rollback [TS]` |
| rave | server + Hermes | "rave mode" by voice | v3.6.7-dj4+ | `./apply-rave.sh dry-run` / `apply` / `rollback [TS]` |
| addonly (stackchan MCP) | Hermes | robot tools for the brain: face, lights, look, photo, dance, music | any | `./apply-addonly.sh dry-run` / `apply` / `rollback` |
| follow-tool | Hermes | "follow me" / "stop following" | v3.6.3+ | `./apply-follow-tool.sh dry-run` / `apply` / `rollback` |
| fun-pack v2 | Hermes | kid memory, games, story time | any | `./apply-fun-pack.sh dry-run` / `apply` / `rollback` |
| agentmail | Hermes | her own email inbox | any | `./apply-agentmail.sh dry-run` / `apply` / `rollback` |
| local-memes | Hermes | makes classic memes on the computer, no cloud | any | `./apply-local-memes.sh dry-run` / `apply` / `rollback [TS]` |
| email-meme | Hermes | emails a saved meme to approved addresses after a read-back | any | `./apply-email-meme.sh dry-run` / `apply` / `rollback [TS]` |
| meme-screen | server + Hermes | shows a saved meme on her screen | any | `./apply-meme-screen.sh dry-run` / `apply` / `rollback [TS]` |
| small-preview | server | sends 160x120 meme previews (same size on screen, a quarter of the memory) | dj6, dj7 | `./apply-small-preview.sh dry-run` / `apply` / `rollback [TS]` |
| meme-fullscreen | server | superseded: full-screen memes for dj6 only; replaced by small-preview | dj6 | `./apply-meme-fullscreen.sh rollback` |

After any Hermes pack: `hermes -p <profile> gateway restart`. Server packs restart only the voice server container.
