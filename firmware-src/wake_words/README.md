# Wake word source (custom_wake_word.cc / .h)

These two files are the wake word detector from the Nous-Girl firmware. They are here so you can see exactly how "Hey Nous-Girl" works and change it to your own phrase. The step-by-step guide is [docs/WAKE-WORDS.md](../../docs/WAKE-WORDS.md).

| File | What it is |
|---|---|
| `custom_wake_word.cc` | The detector. The Nous-Girl phrases, the on/off switches and the sensitivity numbers are in `CustomWakeWord::Initialize`. |
| `custom_wake_word.h` | Its header. Same as upstream, unchanged. |
| `nous-girl-wake-words.patch` | The Nous-Girl change as a patch against the upstream file, if you prefer to apply it with `patch -p1` from the root of a xiaozhi-esp32 tree. |

## Where it comes from

- Upstream project: [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32), MIT License, Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd. and Copyright (c) 2025 Project Contributors. Full text: [licenses/xiaozhi-esp32.txt](../../licenses/xiaozhi-esp32.txt). Listed in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
- Based on upstream commit [`5d54beb`](https://github.com/78/xiaozhi-esp32/commit/5d54beb743ff49c4e8db81bbef9413bdd6e2ba17), the same tree the Nous-Girl builds use. The upstream files have no copyright header of their own; the upstream LICENSE above covers them.
- Nous-Girl changes (MIT, this repo): read the phrases and sensitivity from the robot's settings (`hg` namespace: `ww_nous`, `ww_yub`, `ww_sens`), use English, and log a warning when MultiNet rejects a phrase.

## Where it goes in a xiaozhi-esp32 tree

```
main/audio/wake_words/custom_wake_word.cc
main/audio/wake_words/custom_wake_word.h
```

Copy both over the upstream files (or apply the patch), then build. The file includes `settings.h`, which upstream already has.

**Note:** on its own this file adds the phrases, not the rest of Nous-Girl. "Hey Jarvis" running at the same time, the Settings screen switches and the Jarvis sensitivity live in other Nous-Girl files that are not published yet (see [docs/RELEASING.md](../../docs/RELEASING.md)). In a stock tree, pick **Multinet model (Custom Wake Word)** in menuconfig so the MultiNet model gets packed, as described in the guide.
