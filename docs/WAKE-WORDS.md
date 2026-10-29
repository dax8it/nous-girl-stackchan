# Wake words: switch them, tune them, or make your own

A wake word is what you say to get her attention, like "Hey Nous-Girl". She listens for it **on the robot itself, offline**, using Espressif's speech models (ESP-SR). Nothing is sent anywhere until she hears it.

- [Quick start (no coding)](#quick-start-no-coding)
- [Sensitivity](#sensitivity)
- [The built-in wake words](#the-built-in-wake-words)
- [Make your own phrase](#make-your-own-phrase)
- [Build the firmware](#build-the-firmware)
- [Flash it, and how to undo it](#flash-it-and-how-to-undo-it)
- [Stock XiaoZhi firmware: no code at all](#stock-xiaozhi-firmware-no-code-at-all)
- [Troubleshooting](#troubleshooting)

## Quick start (no coding)

Short version you can share:

> Nous-Girl has three built-in wake words and you can switch them without any coding. Hold the screen for 1.5 seconds to open Settings, tap **Wake words**, and turn "Hey Nous-Girl", "Hey Jarvis" or "Hey YuB" on or off. On the same page, **Sensitivity** sets how easily she wakes: **Low** if she wakes by mistake (TV, noisy room), **High** if she misses you (far away, soft voice), **Normal** is the default. Then tap **Restart to apply**.
>
> A brand-new phrase (your own name for her) needs a firmware rebuild, because the phrase is built into the speech model on the robot. This guide walks you through it.

## Sensitivity

**Where:** Settings (hold the screen 1.5 s) > **Wake words** > **Sensitivity: Low / Normal / High**. The green button is the one in use.

| Setting | What it does | Use it when |
|---|---|---|
| **Low** | She needs a clearer, closer "Hey Nous-Girl" before she wakes. **Fewer false wakes.** | TV or music nearby, a noisy room, she wakes up on her own. |
| **Normal** (default) | Balanced. | Most homes. Start here. |
| **High** | She hears you from farther away and with a softer voice, **but wakes by mistake more often**. | A quiet room, kids or soft voices, she keeps missing you. |

**Tap Restart to apply** after changing it. The new setting is saved, but she only uses it after a restart. It applies to every wake word that is on.

**For the curious:** Sensitivity sets the detection threshold, the score a sound has to reach before it counts (lower number = easier to wake). "Hey Nous-Girl" and "Hey YuB" (MultiNet): Low 0.30, Normal 0.20 (the upstream default), High 0.12. "Hey Jarvis" (WakeNet9): Low 0.72, Normal = the model's own built-in threshold, High 0.55. It is stored on the robot as `hg` / `ww_sens` (0 low, 1 normal, 2 high).

## The built-in wake words

All on Settings > **Wake words**. Every change here needs **Restart to apply**. Full Settings reference: [SETTINGS.md](./SETTINGS.md#wake-words).

| Switch | What she listens for | Default | Notes |
|---|---|---|---|
| **Wake words on** | Master switch. Off = no wake word at all, tap the screen to talk. | on | Quiet time, or if she wakes by mistake too often. |
| **"Hey Nous-Girl"** | Two spellings of the same sound: "hey noose girl" and "hey news girl" (ESP-SR MultiNet7, English). | on | Keeps one CPU core busy while she is idle. Turn it off if her voice sounds choppy on dj7. |
| **"Hey Jarvis"** | A ready-made wake word model (ESP-SR WakeNet9, `wn9_jarvis_tts`). | on | Lighter and very reliable. Good backup word. |
| **"Hey YuB" (try it)** | "hey yub" (rhymes with tub), MultiNet7. | **off** | Short phrase, so it wakes by mistake more. Only for testing. |
| **Sensitivity** | See [Sensitivity](#sensitivity) above. | Normal | |

How they run together: when "Hey Nous-Girl" and "Hey Jarvis" are both on, the robot runs both detectors at once (Jarvis on WakeNet, the custom phrases on MultiNet). If MultiNet can't start, she falls back to "Hey Jarvis" alone.

Stored on the robot as `hg` / `ww_on`, `ww_nous`, `ww_jarvis`, `ww_yub` (defaults 1, 1, 1, 0). A factory reset or a full `0x0` flash puts them back to the defaults.

## Make your own phrase

### How the robot hears a phrase

Custom phrases use **MultiNet7**, Espressif's offline command word model. You give it the phrase as plain lowercase English words, and it works out the sounds itself (a grapheme-to-phoneme step, "G2P"). From the [ESP-SR command word docs](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html):

- MultiNet7 takes ordinary words through the API and converts them to sounds at runtime.
- Use **lowercase letters** (capitals only for an acronym said letter by letter).
- **No numbers or special characters.** Write "two" not "2", and no hyphens, apostrophes or punctuation.
- Don't mix Chinese and English in one phrase.
- Up to 200 phrases in total.

### Spell it the way it sounds

Here is the catch: the G2P step only knows how real English words sound. A made-up name like "Nous" (said "noose") gets guessed wrong. So you **spell the phrase with real English words that sound like it**:

| You want to say | Write it as | Why |
|---|---|---|
| Hey Nous-Girl | `hey noose girl` | "noose" is a real word with the right sound. No hyphen. |
| (same) | `hey news girl` | A near-sound-alike, added as a second line so either way of saying it works. |
| Hey YuB | `hey yub` | Rhymes with tub. |
| Hey Kai-Bot (example) | `hey kai bot` or `hey sky bot` | Split names into simple words; add a sound-alike as a backup. |

This sound-alike spelling is how Nous-Girl's phrases were made. It is our practice built on the ESP-SR rules above, not an Espressif rule. To see what sounds the model will expect, you can run Espressif's own converter, [`tool/multinet_g2p.py`](https://github.com/espressif/esp-sr/tree/master/tool) from the esp-sr repo (`pip install g2p_en pandas numpy`, then `python3 multinet_g2p.py -t "hey noose girl"`). If two spellings print the same sounds, you only need one.

### Tips for a phrase that works

- **3 or more syllables.** "Hey Nous-Girl" (3) works well. "Hey YuB" (2) wakes by mistake more.
- **Avoid everyday words** on their own ("hello", "okay", "computer"), or she wakes during normal talk and TV.
- **Start with "hey"**: it is a clear, easy start and you rarely say "hey" plus her name by accident.
- **Add one near-sound-alike line** (like "news" for "noose") to catch different accents.
- Say it out loud 10 times in a normal voice before you build. If you stumble, pick something easier.

### The exact edit

The file is [firmware-src/wake_words/custom_wake_word.cc](../firmware-src/wake_words/custom_wake_word.cc) (where it goes and where it comes from: [firmware-src/wake_words/README.md](../firmware-src/wake_words/README.md)). In `CustomWakeWord::Initialize` you will find:

```cpp
if (hs.GetInt("ww_nous", 1) != 0) {
    commands_.push_back({"hey noose girl", "Hey Nous-Girl", "wake"});
    commands_.push_back({"hey news girl", "Hey Nous-Girl", "wake"});
}
if (hs.GetInt("ww_yub", 0) != 0) {
    commands_.push_back({"hey yub", "Hey YuB", "wake"});
}
```

Each `push_back` line is one phrase, with three parts:

1. **What the model listens for:** the sound-alike spelling, lowercase, no punctuation (`"hey noose girl"`).
2. **The display name:** the nice spelling (`"Hey Nous-Girl"`). This is the wake word text she reports to the voice server and logs when she wakes. Lines for the same name should use the same display name.
3. **The action:** keep it `"wake"`.

**To replace "Hey Nous-Girl" with your phrase,** change the two lines inside the `ww_nous` block, for example:

```cpp
if (hs.GetInt("ww_nous", 1) != 0) {
    commands_.push_back({"hey sky bot", "Hey SkyBot", "wake"});
    commands_.push_back({"hey kai bot", "Hey SkyBot", "wake"});
}
```

Your phrase then turns on and off with the **"Hey Nous-Girl"** switch (setting key `ww_nous`), and follows **Sensitivity**. The switch label on the Settings screen still says "Hey Nous-Girl"; the label lives in the Settings screen code, not in this file.

**To add a phrase alongside the others,** add one more `push_back` line inside a block. Inside the `ww_nous` block it follows the "Hey Nous-Girl" switch; inside the `ww_yub` block it follows "Hey YuB" (off by default, so turn that switch on).

**To change sensitivity numbers,** edit this line (Low, High, Normal in that order). Lower number = wakes more easily:

```cpp
threshold_ = (sens == 0) ? 0.30f : (sens == 2) ? 0.12f : 0.20f;
```

If MultiNet can't use a phrase (for example it has a digit in it), the boot log shows `wake phrase rejected by MultiNet: <your phrase>`.

## Build the firmware

**Time:** about an hour the first time, mostly installing tools. Written on a Mac; Windows and Linux follow ESP-IDF's own install guide ([PLATFORMS.md](./PLATFORMS.md)).

**Heads-up:** the full Nous-Girl firmware source is not published yet (see [RELEASING.md](./RELEASING.md)). Until it is, you build the upstream XiaoZhi firmware with this wake word file dropped in. That gives you your phrase on a stock XiaoZhi robot, without the Nous-Girl extras (Settings screen switches, "Hey Jarvis" running at the same time, faces, DJ). The phrase edit itself is the one tested on Nous-Girl hardware; this upstream build route follows the upstream build files and has not been tested end to end on a StackChan yet. If you only want one phrase, the [stock menuconfig route](#stock-xiaozhi-firmware-no-code-at-all) needs no code editing at all.

1. **Install ESP-IDF 6.1**, the version every Nous-Girl build used ([TOOLCHAIN-VERSIONS.md](./TOOLCHAIN-VERSIONS.md)):

   ```
   git clone -b v6.1 --recursive https://github.com/espressif/esp-idf
   ```
   ```
   cd esp-idf && ./install.sh esp32s3 && cd ..
   ```
   ```
   source esp-idf/export.sh
   ```

2. **Get the firmware at the same commit Nous-Girl uses:**

   ```
   git clone https://github.com/78/xiaozhi-esp32 && cd xiaozhi-esp32 && git checkout 5d54beb743ff49c4e8db81bbef9413bdd6e2ba17
   ```

3. **Drop in the wake word file** (from this repo's `firmware-src/wake_words/`) and make your edit:

   ```
   cp /path/to/nous-girl-stackchan/firmware-src/wake_words/custom_wake_word.* main/audio/wake_words/
   ```

4. **Settings for the StackChan (CoreS3).** Create a file named `sdkconfig.defaults.mine` in the firmware folder with these lines. The first five are the CoreS3 board settings (from `main/boards/m5stack/core-s3/config.json`); the rest switch on the English MultiNet7 model and custom wake words:

   ```
   CONFIG_BOARD_TYPE_M5STACK_CORE_S3=y
   CONFIG_SPIRAM_MODE_QUAD=y
   CONFIG_CAMERA_GC0308=y
   CONFIG_CAMERA_GC0308_AUTO_DETECT_DVP_INTERFACE_SENSOR=y
   CONFIG_CAMERA_GC0308_DVP_YUV422_320X240_20FPS=y
   CONFIG_LANGUAGE_EN_US=y
   CONFIG_USE_CUSTOM_WAKE_WORD=y
   CONFIG_SR_MN_CN_NONE=y
   CONFIG_SR_MN_EN_MULTINET7_QUANT=y
   ```

5. **Build:**

   ```
   idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.mine" set-target esp32s3
   ```
   ```
   idf.py build
   ```

   To check or change options in a menu instead, run `idf.py menuconfig` before `idf.py build` (board: **Xiaozhi Assistant > Board Type > M5Stack CoreS3**; speech models: **ESP Speech Recognition**). If you edit `custom_wake_word.cc` again later, just run `idf.py build` again.

## Flash it, and how to undo it

Close any serial monitor first. Replace `PORT` with your port (`ls /dev/cu.usbmodem*` on a Mac). Install esptool with `pip install esptool`. Same method as [VERSIONS.md](../VERSIONS.md#how-to-flash).

**1. Back up first** (read only, about 2 minutes). Keep the file private: it holds your Wi-Fi and settings.

```
esptool --chip esp32s3 -p PORT -b 460800 read-flash 0x0 0x1000000 my-stackchan-backup.bin
```

**2. Flash your build** (from the firmware folder). This writes everything your build made, at the addresses your build chose:

```
idf.py -p PORT flash
```

Watch the boot log to see your phrase load (Ctrl+] to quit):

```
idf.py -p PORT monitor
```

Look for `wake phrase rejected` (spelling problem) or the list of active speech commands.

### Rolling back

Run **one** of these, only if something is wrong.

**Put back exactly what you had** (your backup):

```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 my-stackchan-backup.bin
```

**Or go back to the public Nous-Girl firmware** (download the `full-0x0` file from the [releases](https://github.com/dax8it/nous-girl-stackchan/releases) first; this wipes Wi-Fi, so you redo setup after). Two commands, one after the other:

```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after no-reset erase-flash
```
```
esptool --chip esp32s3 -p PORT -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 nous-girl-v3.6.7-dj7-robot-full-0x0.bin
```

## Stock XiaoZhi firmware: no code at all

If you run plain XiaoZhi (not Nous-Girl), you can set **one** custom phrase in menuconfig without touching any code. Build steps are the same as [Build the firmware](#build-the-firmware), but skip step 3 (keep the upstream `custom_wake_word.cc`). In `idf.py menuconfig`:

1. **Xiaozhi Assistant > Wake Word Implementation Type > Multinet model (Custom Wake Word)** (`CONFIG_USE_CUSTOM_WAKE_WORD`). Needs an ESP32-S3 (or P4) with PSRAM, which the StackChan has.
2. **Xiaozhi Assistant > Custom Wake Word** (`CONFIG_CUSTOM_WAKE_WORD`): your phrase, spelled as above, lowercase. The default is the Chinese "xiao tu dou" (pinyin).
3. **Custom Wake Word Display** (`CONFIG_CUSTOM_WAKE_WORD_DISPLAY`): the nice name she sends to the server when she wakes.
4. **Custom Wake Word Threshold (%)** (`CONFIG_CUSTOM_WAKE_WORD_THRESHOLD`): 1 to 99, default 20 (= 0.20). Smaller = wakes more easily.
5. **ESP Speech Recognition > English Speech Commands Model > MultiNet7 quant** (`CONFIG_SR_MN_EN_MULTINET7_QUANT`), and set the **Chinese Speech Commands Model** to **None** for an English phrase. The build stops with an error if custom wake word is on but no MultiNet model is picked.

The build packs these values into the assets image, and the firmware reads them at boot. Then flash and roll back as above.

## Troubleshooting

| Problem | Try this |
|---|---|
| **She wakes by mistake** (TV, talking, music) | Sensitivity **Low**, Restart to apply. Turn off "Hey YuB". For your own phrase: make it longer (3+ syllables) and less like everyday words. |
| **She never wakes** | Check **Wake words on**. Sensitivity **High**, Restart to apply. Say it at a normal speed, facing her, within about 2 m. Try "Hey Jarvis" to check the mic works. For your own phrase: look for `wake phrase rejected` in the boot log, remove digits and punctuation, and add a sound-alike line. |
| **Only "Hey Jarvis" works** | "Hey Nous-Girl" may be off, or MultiNet failed to start and she fell back to Jarvis. Turn the switch on and restart; check the boot log for `MultiNet wake word failed`. |
| **Choppy or stuttering voice on dj7** | Known dj7 issue: "Hey Nous-Girl" keeps one CPU core busy, which can starve the audio (log lines like `AFE(FEED) ringbuffer full`). Turn **"Hey Nous-Girl" off**, keep "Hey Jarvis" on, Restart to apply. |
| **Changes did nothing** | Wake word changes only count after **Restart to apply**. |
| **Wake word doesn't stop her while she talks** | On purpose. This board has no echo cancelling, so her own voice caused false stops. **Tap the screen** to interrupt her. |

Still stuck? See [Where to get help](../README.md#where-to-get-help).
