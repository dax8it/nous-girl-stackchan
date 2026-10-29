# The Settings screen, every option explained

Everything on Nous-Girl's own Settings screen: what each button and switch does, its default, how to use it, and when you might change it. Written from the firmware source of **v3.6.7-dj7** (public build). Older versions have fewer pages; see [VERSIONS.md](../VERSIONS.md).

> **Beta:** the public builds are checked by scan and checksum but not yet tested on anyone else's robot. If something here doesn't match what you see, please [open an issue](https://github.com/dax8it/nous-girl-stackchan/issues).

## Contents

- [Open, move around, close](#open-move-around-close)
- [Screen gestures and buttons (outside Settings)](#screen-gestures-and-buttons-outside-settings)
- [Home page](#home-page)
- [Wi-Fi](#wi-fi) · [Wake words](#wake-words) · [Sound](#sound) · [Screen](#screen) · [Head](#head) · [Lights](#lights) · [Dance](#dance) · [Music](#music) · [Server](#server) · [Voice & AI](#voice--ai) · [Camera & SD](#camera--sd) · [Language](#language) · [About](#about) · [Restart](#restart)
- [Sleep and power saving](#sleep-and-power-saving)
- [Where settings are stored](#where-settings-are-stored)

## Open, move around, close

| To do this | Do this |
|---|---|
| **Open Settings** | Hold one finger still on the screen for **1.5 seconds**, then lift. (A new robot with no server opens Settings > Server by itself.) |
| Open by voice | Say "open settings" (or "open the Wi-Fi settings"). This only works when your voice server lets her brain use the robot's own tools; with the plain Hermes setup it may not. Holding the screen always works. |
| Go to a page | Tap its colored tile on the home page. |
| Go back | Tap the **blue arrow** (top left). It goes back to the home page. |
| Close | Tap the **red X** (top right). |
| Auto close | Settings closes by itself after **60 seconds** with no finger on the glass. Scrolling and sliding count as touches. |
| Scroll | Drag the page up or down. Long pages (Wake words, Lights, Music) scroll. |

**When does a change take effect?** Switches, sliders and buttons work **right away and are saved** (they survive a restart), unless the table says otherwise. Two exceptions: **wake word** changes need **Restart to apply**, and a **server address** only counts after **Save + restart**.

## Screen gestures and buttons (outside Settings)

These work on her normal face screen, not inside Settings.

| Gesture | What happens |
|---|---|
| **Tap** | Starts a conversation, or stops her while she's talking (tap to interrupt). While she is still starting up, a tap opens Wi-Fi setup mode instead. |
| **Hold 1.5 s** | Opens Settings. |
| **Swipe right to left** | Next song, while music plays or on the DJ screen. Ignored when no music is playing. |
| **Swipe left to right** | Previous song (same rules). |
| Swipe up or down | Does nothing on purpose (rave mode is voice only). |
| Tap while a meme is on screen | Hides the meme. Nothing else happens. |
| **Power key, short press** | Switches only the face style: drawn starter face or the classic StackChan line face. Saved. No restart, no change to server or voice. |
| Head pads (3-pad sensor on top) | Tap, hold or swipe her head. Swipe forward = next song or dance, swipe back = stop. Flip the direction in [Head](#head). |

A tap is a touch that moves less than about 24 pixels. A swipe has to travel at least 50 pixels within 1.2 seconds.

## Home page

14 tiles, in this order: **Wi-Fi, Wake words, Sound, Screen, Head, Lights, Dance, Music, Server, Voice & AI, Camera & SD, Language, About, Restart.** The line at the bottom reminds you how to open Settings again.

---

## Wi-Fi

What she's connected to, and how to add a network.

| Item | What it does | Default | When to use it |
|---|---|---|---|
| Network / Signal / IP | Shows the Wi-Fi name, signal strength (dBm, closer to 0 is better; below about -75 is weak), channel and her IP address. | read only | Checking that she's online, or finding her IP. |
| **Reconnect** | Drops Wi-Fi and joins again. | | She shows a network error but your Wi-Fi is fine. |
| **Wi-Fi setup mode** | Restarts her into her own setup hotspot, **`Xiaozhi-XXXX`**. Join it from your phone and add a network ([step-by-step](./SELF-HOSTING.md#step-2-put-the-robot-on-your-wi-fi)). While in setup mode she keeps looking and joins any saved network that shows up. | | Adding a new network, a phone hotspot, or fixing a wrong password. |
| Saved networks (N of 10) | Lists saved network **names** (never passwords), up to 10. | read only | Checking which networks she knows. |

**Good to know:** only **2.4 GHz** Wi-Fi works. On an iPhone hotspot, turn on **Maximize Compatibility**. A phone hotspot is saved even if the first test fails, and she joins it when it appears. To forget all networks, use **Factory reset** in [Restart](#restart).

## Wake words

How she knows you're talking to her.

| Item | What it does | Default | When to change it |
|---|---|---|---|
| **Wake words on** | Master switch. Off = no wake word at all; tap the screen to talk. | on | Quiet time, or if she wakes by mistake too often. |
| **"Hey Nous-Girl"** | The custom phrase, heard by the on-robot speech model (ESP-SR MultiNet). It listens for "hey noose girl" and "hey news girl". | on | Turn off if you see choppy audio on dj7; it keeps one CPU core busy while she's idle. |
| **"Hey Jarvis"** | A ready-made wake word model (ESP-SR WakeNet9). Lighter and very reliable. | on | Keep it on as the backup word. |
| **"Hey YuB" (try it)** | A short experimental phrase. | **off** | Only if you want to test it. Short phrases wake by mistake more. |
| Interrupt with wake word | Hidden in public dj7. It was tried in v3.6.5 and switched off in v3.6.6 because this board has no echo cancelling, so her own voice caused false stops. The page says "To stop her while she talks: tap the screen." | (not shown) | |
| **Sensitivity: Low / Normal / High** | How easily a wake word counts. High hears you from farther away but wakes by mistake more. The green button is the current one. | **Normal** | Low for a noisy room or TV nearby, High for a quiet room or a soft voice. |
| **Restart to apply** | Restarts her so the wake word changes take effect. | | **After any change on this page.** |

**For the curious:** Sensitivity sets the detection threshold. "Hey Nous-Girl": Low 0.30, Normal 0.20 (the upstream default), High 0.12. "Hey Jarvis": Low 0.72, Normal = the model's own threshold, High 0.55.

## Sound

| Item | What it does | Default | When to use it |
|---|---|---|---|
| Status line | Current volume, whether the speaker is on or idle, and its sample rate. | read only | |
| **Volume** slider | 10 to 100. It can't go below 10, so she can never be stuck silent. Saved. | 70 | Any time. You can also say "turn it down". |
| **Test speaker** | Plays two beeps and a pop straight from her memory, no network. | | No voice? If you hear the beeps, the speaker is fine and the problem is the network or server. |
| **Volume 80** | One tap to set volume 80. | | Quick reset after it got too quiet. |

## Screen

| Item | What it does | Default | When to change it |
|---|---|---|---|
| **Brightness** slider | 10 to 100. Saved. | 75 | Dark room: lower. Sunny desk: higher. |
| **Lip sync (mouth follows voice)** | Her mouth moves with the loudness of her voice. | on | Off if you prefer a still face while she talks. |
| **Vowel mouth shapes (a i u e o)** | The mouth takes vowel shapes instead of just opening and closing. Needs lip sync on. | on | Off for the simpler open/close mouth. |
| **Talking lights** | Her LEDs get brighter and dimmer with her voice. | on | Off if the lights distract you. |

## Head

Her two servos: left/right (yaw) and up/down (pitch).

| Item | What it does | Default | When to use it |
|---|---|---|---|
| Rest tilt | Her resting up/down angle in degrees. **3 = level, facing forward** (also her lowest). | 3 | |
| **Lower / Raise** | Moves the rest tilt 1 degree and saves it as her new home. | | She looks too far up or down when resting. |
| **Go home** | Moves her head back to home (centered, rest tilt). | | After a dance or if she ended up looking sideways. |
| **Reset to level** | Sets rest tilt back to 3 and saves. | | Undo your Lower/Raise changes. |
| **Look around when idle** | Small random head moves while idle, so she looks alive. Saved. | on | Off for a still robot, for example on a shelf. You can also say "stop moving around". |
| **Follow faces (camera)** | Her head follows the nearest face using on-robot face detection, and drifts home when nobody is there. Saved. Needs Camera on. | off | On when you want her to watch you. |
| Follow faces debug | Writes one log line per second about face tracking to the USB serial log. | off | Only when fixing face follow. Leave off. |
| **Swap head swipe direction** | Flips the head-pad swipe: normally forward = next song or dance, back = stop. | off | If the swipe feels backwards. |

## Lights

Her 12 RGB LEDs. These buttons change the lights **now**; they are not saved and go back to automatic after a restart.

| Item | What it does |
|---|---|
| **On** | Lights on. |
| **Off** | Lights off. |
| **Auto** | Back to automatic colors that follow what she's doing (listening, thinking, speaking). Use this to undo any custom color. |
| **Colors** | red, orange, yellow, green, blue, purple, pink, white. The color stays until you tap Auto. |
| **Moods** | happy, calm, party, spooky, sleepy, focus: color patterns. |
| **Rainbow slow / Rainbow fast** | A rainbow that cycles. |
| **Light brightness (next change)** | 5 to 100, default 80. Sets the brightness for the **next** button you tap; it doesn't change the current light by itself. Goes back to 80 after a restart. |

By voice (needs the robot tools in her brain, see [HERMES.md](./HERMES.md)) she knows more colors, like teal, gold or warm white, and a "breathe" effect.

## Dance

| Item | What it does |
|---|---|
| **Preset buttons**: wiggle, nod, disco, robot, sway | Plays that dance twice: head moves, lights and face. |
| **Learn a dance** | Starts recording her head moves while you steer her with the **K151-R remote** (an M5StickC Plus with a joystick hat that talks to the robot over ESP-NOW, no router needed). Records every 100 ms, up to 60 seconds. |
| **Stop + save** | Shown while recording. Saves the dance to the **microSD card** as `my_dance_<number>`. |
| **Stop** | Stops any dance now. |
| **My dances** | Your saved dances from the SD card. Tap one to play it. "none yet" if the card has none or there's no card. |

**Remote tip:** the remote's Channel (in its SETUP page) must match your Wi-Fi router's channel, because the robot never changes its Wi-Fi channel. [Wi-Fi](#wi-fi) shows the channel.

## Music

| Item | What it does | Default | When to change it |
|---|---|---|---|
| **Dance to music** | She grooves with head and lights to any song that plays, and stops when the song ends. Saved. | on | Off if you want her still while music plays. |
| **DJ mode (DJ screen + groove while a Mac song plays)** | While a song from your **server's** music folder plays, she shows the DJ screen: her face over a 13-band equalizer that moves to the music, colors on the beat, and grooving. Saved. Same switch as saying "DJ mode" / "DJ mode off". | **off** | On for parties and music time. |
| **Random** | Plays a random MP3 from the SD card's `/music` folder. | | |
| **Stop** | Stops SD card music. | | |
| Playing | The SD song playing now. | read only | |
| Songs on the SD card | Up to 24 songs from `/music`, one button each. "No MP3s in /music" if empty. | | Tap a song to play it. |

**Two kinds of music:** SD card songs play right on the robot (this page). Songs on your computer play through the voice server: say "play music" ([FEATURES.md](./FEATURES.md#music)). The SD card must be FAT32, 32 GB or smaller.

## Server

Where she connects. **Public firmware has no server built in**, so this is empty on a new robot. Full first-boot guide: [SELF-HOSTING.md](./SELF-HOSTING.md#first-boot-step-by-step).

| Item | What it does |
|---|---|
| Server (OTA) address now | The saved address, or "(none yet: type your server address below)". |
| Text box | Tap it and a keyboard appears. Type the address. Tap the check key or the keyboard key to hide the keyboard. |
| **LAN example** | Fills in a sample home address for you to edit: change the IP to your computer's. |
| **Save + restart** | Saves the address and restarts. It must start with `http://` or `https://`, or it is **not saved** (nothing happens). |

**What to type:**

| Your setup | Address |
|---|---|
| Your own server at home (recommended) | `http://<your-computer-ip>:8003/xiaozhi/ota/` |
| Your own server on your own domain | `https://<your-domain>/xiaozhi/ota/` |
| The official xiaozhi cloud (no add-ons) | `https://api.tenclass.net/xiaozhi/ota/` |

**Easier than the tiny keyboard:** [Wi-Fi](#wi-fi) > **Wi-Fi setup mode**, join `Xiaozhi-XXXX` from your phone, open the **Advanced** tab and paste the address into **Custom OTA URL**.

## Voice & AI

Information only. Her voice, brain (Hermes or a plain model) and hearing all run on your **server** and are chosen in the server's config (`.config.yaml`), not on the robot. See [SELF-HOSTING.md](./SELF-HOSTING.md) and [HERMES.md](./HERMES.md).

## Camera & SD

| Item | What it does | Default | When to change it |
|---|---|---|---|
| **Camera** | Lets her take photos ("what do you see?", "who is here?") and follow faces. Off = no photos and no face follow. Saved. | on | Off for privacy. |
| SD card | Shows whether a microSD card is in and readable. | read only | Checking the card before music or saved dances. |

## Language

Information only: **English (US)**. Her on-screen text and built-in sounds are English. This is **not** what language she speaks: talking in Spanish, French, German or Portuguese is a server feature ([multilingual voice](./FEATURES.md#multilingual-voice)). Other on-robot languages would need a different firmware build.

## About

| Line | What it means |
|---|---|
| `Nous-Girl public v3.6.7-dj7 (<face> face)` | The build and which face it has. |
| Firmware ... (date) | Version string and build date. |
| MAC | Her Wi-Fi hardware address. Some routers and servers use it to recognize her. Treat it as private. |
| Free RAM / PSRAM | Free memory right now, in KB. Mostly for bug reports. |
| Server | The saved server address. |
| **Started because** | Why she last started: power on, restart, crash, and so on. |
| **Last crash** | The last unexpected restart, with memory and battery at that time, or "none recorded". |

**Reporting a bug?** Take a photo of this page and add it to your issue.

## Restart

| Item | What it does |
|---|---|
| **Restart** | Restarts her. Nothing is lost. |
| **Factory reset** | Tap once: the button turns into **"Tap again: ERASE ALL"**. Tap again to erase. It forgets **Wi-Fi networks, the server address and every setting on these pages**, then restarts into first boot. Your **SD card and songs are not touched**, and the firmware stays the same. Leaving the page cancels it. |

## Sleep and power saving

There is no sleep timer in Settings. It's built in:

- **On battery:** after about **5 minutes (300 s)** with nothing happening, the screen dims to the lowest brightness to save power. Talk to her, tap, or say the wake word and it comes back to your brightness.
- **On USB power:** the screen doesn't dim.
- She **never turns herself off** on a timer.
- The "sleeping" face is a mood the brain can choose. It does not turn anything off.

## Where settings are stored

Everything is stored in the robot's flash memory (NVS), so it survives restarts and firmware updates that keep Wi-Fi (app-only flashing). A full `0x0` flash or **Factory reset** erases it. For people building or debugging firmware:

| Setting | NVS namespace / key | Default |
|---|---|---|
| Wake words on, Hey Nous-Girl, Hey Jarvis, Hey YuB | `hg` / `ww_on`, `ww_nous`, `ww_jarvis`, `ww_yub` | 1, 1, 1, 0 |
| Wake word sensitivity | `hg` / `ww_sens` (0 low, 1 normal, 2 high) | 1 |
| Lip sync, vowel shapes, talking lights | `hg` / `lipsync`, `vowel`, `talklight` | 1, 1, 1 |
| Dance to music, DJ mode, camera | `hg` / `groove`, `dj`, `camera` | 1, 0, 1 |
| Swap head swipe | `hg` / `swipeswap` | 0 |
| Face style (0 drawn, 1 classic) | `hg` / `face_style` | 0 |
| Look around when idle | `fairy_motion` / `auto_motion` | on |
| Follow faces, follow debug | `fairy_gaze` / `follow`, `debug` | off, off |
| Head home position | `fairy_servo` / `home` | level |
| Volume | `audio` / `output_volume` | 70 |
| Brightness | `display` / `brightness` | 75 |
| Server address | `wifi` / `ota_url` | empty in public builds |
