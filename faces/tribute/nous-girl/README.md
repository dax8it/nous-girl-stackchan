# Nous-Girl tribute face

![Nous-Girl tribute sprite sheet](preview.png)

This is the face the maintainer's own robot wears: a headphone-wearing girl drawn as a **fan-made tribute (homage) to Nous Research's Nous-Girl character**.

## Please read

- This is fan art. It is **not affiliated with, sponsored by or endorsed by Nous Research**.
- The Nous-Girl character, her name and her likeness belong to Nous Research (or their respective owners). No rights to the character are claimed or granted here.
- This folder is **NOT covered by the repo's MIT license**, and it is not CC0 like `faces/starter/`. No open license is given for it.
- It is shared for **personal, non-commercial use only**, such as putting it on your own StackChan. Do not sell it, ship it in a product or use it to advertise anything.
- **If Nous Research or any rights holder asks, it will be removed.** Open an issue or contact the maintainer through GitHub and it will be taken down quickly.

Want a face you can share and change freely? Use the CC0 [starter faces](../../starter/README.md) or [make your own](../../../docs/SPRITES.md).

## What is in here

| File | What it is |
|---|---|
| `gifs/` | the 12 firmware-ready animations (320x240) plus `_emote_aliases.json`, the same files the maintainer's robot runs |
| `hg_mouth_frames.h` | the 8 lip-sync mouth pictures for the firmware |
| `hg_face_layout.h` | where the scowl brows and anger mark sit on this face |
| `sprite_sheet.png`, `sprite_sheet.json` | every frame in one sheet (320x240 cells) with row names and per-frame timing |
| `mouth_preview_sheet.png` | the mouth shapes side by side |
| `preview.png` | small preview of the sheet |

## Use it on your own robot

Same steps as a starter face (details in [docs/SPRITES.md](../../../docs/SPRITES.md)):

1. Copy everything in `gifs/` into the firmware's `fairy-assets/` folder (replace the GIFs there).
2. Copy `hg_mouth_frames.h` and `hg_face_layout.h` into `main/boards/m5stack/core-s3/`.
3. Build and flash the app as usual.

No prebuilt firmware with this face is published.
