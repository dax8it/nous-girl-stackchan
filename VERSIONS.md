# Nous-Girl firmware versions and checkpoints

Every firmware build, oldest to newest, with what it added, what's known to be wrong with it, its SHA-256, and how to flash it or roll back to it. If you want to start from an earlier version, pick a row and use its flash command.

> **Current recommended: `v3.6.7-dj7`** (Oct 28, 2026), with a robot (default), boy or girl face.
> **Safe fallback: `v3.6.7-dj5`.** Avoid `v3.6.7-dj6`: it can reboot while showing a meme preview.
>
> **Beta:** builds verified by scan and checksum, not yet tested on hardware. All public builds are pre-releases. Please [report issues](https://github.com/dax8it/nous-girl-stackchan/issues).

All times are US Eastern. All builds target the **M5Stack StackChan K151 (CoreS3, ESP32-S3, 16 MB flash)**, built with ESP-IDF 6.1.0 (see [docs/TOOLCHAIN-VERSIONS.md](./docs/TOOLCHAIN-VERSIONS.md)). Upstream bases: [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) and fairy-stackchan.

## Where the binaries are

Binaries are **not committed to git**. Four checkpoints have a [GitHub Release](https://github.com/dax8it/nous-girl-stackchan/releases) with **public builds**: **v3.6, v3.6.6, v3.6.7-dj5 and v3.6.7-dj7**. Every other version is described here for history, with no download.

A public build is that version rebuilt with the CC0 starter faces ([faces/starter/](./faces/starter/)) and **no server address inside**, not even the upstream cloud. An unconfigured robot connects nowhere and opens Settings > Server; you type your own address ([first boot steps](./docs/SELF-HOSTING.md#first-boot-step-by-step)). Settings > About says `Nous-Girl public <version> (<face> face)`. Details: [docs/RELEASING.md](./docs/RELEASING.md).

Each release has, per face:

| File | Flash at | Use |
|---|---|---|
| `nous-girl-<version>-<face>-full-0x0.bin` | `0x0` | first install, from stock firmware or an older layout (wipes Wi-Fi) |
| `nous-girl-<version>-<face>-app.bin` | `0x20000` | update or roll back, keeps Wi-Fi |
| `nous-girl-<version>-<face>-assets.bin` | `0xB00000` | faces and wake-word models; flash with the app when you switch faces |

v3.6.7-dj7 comes in `robot`, `boy` and `girl`. The other checkpoints come in `robot` only; to use another face with them, build it yourself ([docs/SPRITES.md](./docs/SPRITES.md)).

Checksums: [firmware/SHA256SUMS](./firmware/SHA256SUMS) and `SHA256SUMS` in each release. Check before you flash:

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
| v3.4 | Oct 22 | Shake reaction ends properly | old | last old-layout checkpoint (no public build) |
| v3.5 | Oct 23 | Vowel lip sync, party face, head touch, talking lights | old | superseded |
| v3.6 | Oct 23 | On-device face detection, **new partition table** | new | **public checkpoint**, first on the new layout |
| v3.6.1 | Oct 23 | Memory fix for connect errors, level head | new | superseded |
| v3.6.2 | Oct 23 | Photo / face ID reliability | new | superseded |
| v3.6.3 | Oct 23 | Follow faces actually runs, voice switch | new | superseded |
| v3.6.4 | Oct 23 | Photo fixes, step-by-step photo logs | new | superseded |
| v3.6.5 | Oct 23 | Battery power-on fix, phone hotspot setup | new | superseded |
| v3.6.6 | Oct 23 | Crash report at boot, no Wi-Fi scan while connected | new | **public checkpoint**, last before DJ |
| v3.6.6-ir | Oct 24 | Side build: IR remote learn/send | new | experimental, never run on hardware |
| v3.6.7-dj | Oct 27 | DJ mode, set lists, swipe next/prev, scowl face | new | superseded |
| v3.6.7-dj2 | Oct 27 | 13-band equalizer screen | new | superseded |
| v3.6.7-dj3 | Oct 27 | Color cycling EQ, beat flash, LEDs match | new | superseded |
| v3.6.7-dj4 | Oct 27 | Rave mode | new | superseded |
| v3.6.7-dj5 | Oct 27 | Reliable swipes, rave by voice only | new | **public checkpoint**, fallback |
| v3.6.7-dj6 | Oct 28 | Full-screen meme tool | new | **known crash, avoid** |
| v3.6.7-dj7 | Oct 28 | Meme preview crash fixed, memory freed | new | **public checkpoint**, current recommended |

"Layout" is the flash partition table. **old** = app slots at 0x20000/0x410000, assets at 0x800000. **new** (`16m_hg36`, from v3.6) = two 5.4375 MB app slots at 0x20000/0x590000, assets 5 MB at 0xB00000. Moving between old and new needs one full flash (see [Moving between layouts](#moving-between-layouts)).

## How to flash

Close any serial monitor first. Opening the port resets the robot. Replace `PORT` with your port (`ls /dev/cu.usbmodem*` on a Mac). Install esptool with `pip install esptool` (or `brew install esptool`).

**Always back up first** (read only, about 2 minutes):

```
esptool --chip esp32s3 -p PORT -b 460800 read-flash 0x0 0x1000000 my-stackchan-backup.bin
```

Keep that file private: it holds your Wi-Fi and settings.

**First install** (from stock firmware, an older layout, or if unsure). Wipes Wi-Fi and settings, so you redo Wi-Fi setup after. Pick your face:

```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after no-reset erase-flash
```
```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 nous-girl-v3.6.7-dj7-robot-full-0x0.bin
```

**Update, roll back or switch face** (already on v3.6 or later, keeps Wi-Fi and settings). Flash the app and the assets of the same version and face:

```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x20000 nous-girl-v3.6.7-dj7-robot-app.bin 0xb00000 nous-girl-v3.6.7-dj7-robot-assets.bin
```

Swap the version or face in the file names for any other public build. Settings > About shows the version and face afterwards.

**Rolling back** is the same command with an older checkpoint (v3.6.7-dj5, v3.6.6, v3.6). Roll back one step at a time, and only when something is broken. Don't run several rollback commands in a row: the last one wins.

**Restore your backup** (exactly what you had before):

```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 my-stackchan-backup.bin
```

### Moving between layouts

- **To the new layout** (stock firmware or anything up to v3.5): use the **first install** above. Every `full-0x0` image already has the new partition table.
- **Back to the old layout** (v3.5 or earlier): there is no public old-layout build. Restore your own backup.

## Version notes

File names in these notes are the maintainer's own builds, kept for history. They are not published; use the public checkpoints above.

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

Public builds only. Same list as [firmware/SHA256SUMS](./firmware/SHA256SUMS).

| Version | Face | File | SHA-256 |
|---|---|---|---|
| v3.6.7-dj7 | boy | `nous-girl-v3.6.7-dj7-boy-app.bin` | `fb17929f93d98462b78dbe5a4c0c3279e9cd84dd7f5a77bd047856b701eebed2` |
| v3.6.7-dj7 | boy | `nous-girl-v3.6.7-dj7-boy-assets.bin` | `dffa0340c81bc7b0336d8f1b21d0d48730a44d5b17b2fc2dd54e960c357cdc53` |
| v3.6.7-dj7 | boy | `nous-girl-v3.6.7-dj7-boy-full-0x0.bin` | `cc61838bcd16f569c13aa1779fda7ef6da9b6994094f4025e73145bedaafce2f` |
| v3.6.7-dj7 | girl | `nous-girl-v3.6.7-dj7-girl-app.bin` | `bccc5b8be15d5a16bc076ffa34d759b55758c1644ba0383948d180a57e17605d` |
| v3.6.7-dj7 | girl | `nous-girl-v3.6.7-dj7-girl-assets.bin` | `268390a3dc25856069aeaaec27db938114405f8c28ebc39f5a6db15f3fe64d8d` |
| v3.6.7-dj7 | girl | `nous-girl-v3.6.7-dj7-girl-full-0x0.bin` | `eeabb36c07c9583c9d33af4f74e9f39ef84d40ece1674b7684811fbc1c03de85` |
| v3.6.7-dj7 | robot | `nous-girl-v3.6.7-dj7-robot-app.bin` | `717704999fbf59b2c4a04ebed572f64cd32f0a29cca8fe7442d35648e2c9fbdb` |
| v3.6.7-dj7 | robot | `nous-girl-v3.6.7-dj7-robot-assets.bin` | `699f7f52029c1657d59194f72f373122ce36fa1a517a7417e38982a63a303cd8` |
| v3.6.7-dj7 | robot | `nous-girl-v3.6.7-dj7-robot-full-0x0.bin` | `62b85e98aa961b5ee61af8ace44596a1a74efd582b5dbf4389dd137055535162` |
| v3.6.7-dj5 | robot | `nous-girl-v3.6.7-dj5-robot-app.bin` | `de4c2cf5f48fc33a7c410edb3b63a648a6f59683eba414d659ae0497db9b2e31` |
| v3.6.7-dj5 | robot | `nous-girl-v3.6.7-dj5-robot-assets.bin` | `699f7f52029c1657d59194f72f373122ce36fa1a517a7417e38982a63a303cd8` |
| v3.6.7-dj5 | robot | `nous-girl-v3.6.7-dj5-robot-full-0x0.bin` | `59504ea171d477a531f087c45ff4d766a43b6562cb1d05e0b168fd0f9d03f44f` |
| v3.6.6 | robot | `nous-girl-v3.6.6-robot-app.bin` | `7887a1e81ef8046caea9a0300cdf7b8e0142d12aaf8e46cf9ef6d87e7b7e051b` |
| v3.6.6 | robot | `nous-girl-v3.6.6-robot-assets.bin` | `699f7f52029c1657d59194f72f373122ce36fa1a517a7417e38982a63a303cd8` |
| v3.6.6 | robot | `nous-girl-v3.6.6-robot-full-0x0.bin` | `630fa8ba2382eb5130bfdd17e46099d38df591923bd188a4099071d00544b30b` |
| v3.6 | robot | `nous-girl-v3.6-robot-app.bin` | `bab665cb0407444c99803feea049228d0ab0a0184db13d1781a790338930b79a` |
| v3.6 | robot | `nous-girl-v3.6-robot-assets.bin` | `699f7f52029c1657d59194f72f373122ce36fa1a517a7417e38982a63a303cd8` |
| v3.6 | robot | `nous-girl-v3.6-robot-full-0x0.bin` | `eda082e40e55585541e3af27f8511203189bff3985713a70490af8e56c9f5a5a` |

The robot assets image is the same file in every checkpoint, so its checksum repeats.

Building from source instead: the patched firmware source for each version is not public yet (see the open items in [docs/RELEASING.md](./docs/RELEASING.md)). The toolchain is in [docs/TOOLCHAIN-VERSIONS.md](./docs/TOOLCHAIN-VERSIONS.md).

## Server and Hermes add-ons

Many features live on the server or in the Hermes brain, not in the firmware. They are in [server/](./server/README.md) as add-ons, installed one at a time with one script: `./install-addon.sh <name> status` (read only), `dry-run` (shows changes, writes nothing), `apply` (backs up first, rolls back by itself if the server doesn't come back), `rollback [TS]` (puts the backup back). What each feature does is in [docs/FEATURES.md](./docs/FEATURES.md).

| Add-on | Side | What it does | Needs firmware |
|---|---|---|---|
| [patched-server](./server/addons/patched-server/) | server | **install first**: hg bridge, real next/previous, DJ, radio, rave and Actor Mode code, meme previews, screen text cleanup | any |
| [dj-mode](./server/addons/dj-mode/) | server | example DJ set list | v3.6.7-dj5+ |
| [radio](./server/addons/radio/) | server | internet radio stations | v3.6.7-dj3+ |
| [rave-mode](./server/addons/rave-mode/) | server + Hermes | "rave mode" by voice | v3.6.7-dj5+ |
| [actor-mode](./server/addons/actor-mode/) | server | example Actor Mode scripts ([guide](./docs/ACTOR-MODE.md)) | any |
| [multilingual](./server/addons/multilingual/) | server | Phonon voices in en/es/fr/de/pt, picked per sentence | any |
| [local-stt-mlx](./server/addons/local-stt-mlx/) | host | optional local speech-to-text on Apple Silicon | any |
| [stackchan-tools](./server/addons/stackchan-tools/) | Hermes | robot tools for the brain: face, lights, look, photo, dance, music, DJ, radio, follow faces, memes | any (some tools need newer) |
| [local-memes](./server/addons/local-memes/) | Hermes | classic memes made on the computer, no cloud | any |
| [meme-previews](./server/addons/meme-previews/) | server + Hermes | 160x120 meme previews on her screen | dj7 |
| [email-meme](./server/addons/email-meme/) | Hermes | emails a saved meme to approved addresses after a read-back | any |
| [agentmail](./server/addons/agentmail/) | Hermes | her own email inbox | any |
| [fun-pack](./server/addons/fun-pack/) | Hermes | kid memory, games, story time | any |
| [hermes-profile](./server/addons/hermes-profile/) | Hermes | example `.env` with the names the add-ons use | |

After any Hermes add-on: `hermes -p <profile> gateway restart`. Server add-ons restart only the voice server container (`docker restart`, never a recreate).
