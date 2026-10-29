# Make your own face

Public builds come with three original starter faces: **robot** (default), **boy** and **girl**. They are CC0 (public domain) and drawn by a script, so you can copy, change or replace them. This page explains the exact format, how to get an AI image model to draw a new face, and how to turn the pictures into firmware.

![Starter faces](../faces/starter/starter-faces-combined.png)

**Start from the template:** [`faces/starter/robot/sheet.png`](../faces/starter/robot/sheet.png) shows every frame the firmware needs, in order. Copy it, draw over it, keep the sizes.

## The format (must match exactly)

- **Screen:** 320 x 240 pixels. Every frame is a full screen.
- **Files:** animated GIFs, 320 x 240, loop forever, **no transparency** (opaque, white background).
- **Colours:** black ink on white works best. Rave mode tints the white parts, so dark lines on white look great and coloured fills look muddy. The starter faces use 6 greys: 0, 70, 140, 200, 235, 255.
- **Keep the bottom free:** the DJ equalizer draws at y 186 to 236. Put eyes and mouth above y 186.
- **Size budget:** all 12 GIFs together should stay under about **850 KB** (starter faces are 170 to 185 KB). Fewer colours and flat areas keep GIFs small.

### The 12 animations

File names must match. Frame counts and times below are what the starter faces use; other counts work too.

| File | Frames | Frame times (ms) | What it shows |
|---|---|---|---|
| `neutral.gif` | 3 | 2400, 120, 600 | resting, frame 2 is the **blink** |
| `happy.gif` | 4 | 160 each | smiling, bouncy |
| `sad.gif` | 5 | 300, 220, 220, 220, 400 | sad, a tear |
| `thinking.gif` | 4 | 350, 350, 350, 1000 | eyes up, thinking dots |
| `sleeping.gif` | 6 | 420 each | eyes closed, Z z z |
| `pet.gif` | 6 | 160 each | loving, hearts (head touch) |
| `shake.gif` | 8 | 90 each | dizzy, shaking |
| `speaking.gif` | 5 | 140, 160, 120, 300, 160 | talking (used when lip sync is off) |
| `listening.gif` | 3 | 220, 220, 620 | attentive, eyebrows up |
| `look_left.gif` | 6 | 200, 90, 90, 1000, 90, 90 | glance left and back |
| `look_right.gif` | 5 | 200, 90, 790, 300, 200 | glance right and back |
| `email.gif` | 12 | your choice | a letter arrives |

Also needed: `_emote_aliases.json` (copy it from a starter face; it maps names like `loving` or `angry` to these files).

### Lip sync (mouth pictures)

While she talks, the firmware draws a small mouth picture over the face. You need **8 mouths** for the neutral face and **7** (no smile) for each of **sad, thinking, pet**:

| # | Name | Sounds like |
|---|---|---|
| 0 | closed | silence, m, b, p |
| 1 | half | quiet sounds |
| 2 | open | "ah" |
| 3 | wide_e | "eh" |
| 4 | round_o | "oh" |
| 5 | small_u | "oo" |
| 6 | wide_i | "ee" |
| 7 | smile | neutral only |

Rules:
- All mouths live in one **mouth box**. Starter faces use x 128, y 140, w 64, h 36.
- In `neutral`, `sad`, `thinking` and `pet`, **the mouth box must not move** between frames, so the overlay matches.
- Each mouth picture is the full 320 x 240 face with that mouth. Only the box is used.

### Eyes, scowl and anger mark

Angry faces draw two eyebrow lines and a small anger mark in code, on top of your art. Their positions are in `hg_face_layout.h`:

```c
#define HG_SCOWL_A0 {136, 96}   // left brow, inner end
#define HG_SCOWL_A1 {100, 84}   // left brow, outer end
#define HG_SCOWL_B0 {184, 96}   // right brow, inner end
#define HG_SCOWL_B1 {220, 84}   // right brow, outer end
#define HG_ANGER_X 228          // anger mark, top-left corner
#define HG_ANGER_Y 34
```

Starter eyes sit at x 118 and x 202, y 104. If your eyes are elsewhere, change these numbers.

## Draw it with an AI image model

AI image models are good at one consistent character, and bad at exact pixel sizes. So ask for **big square key pictures** of the same character, then let the scripts resize and animate them.

1. Make **one** reference picture first. Example prompt:

   > Front-facing cartoon face of a friendly [robot / kid / animal], centred, filling the frame, thick clean black outlines on a plain white background, flat shading, no text, no body below the chin, simple shapes, kid-friendly. Square, 1024x1024.

2. Keep that picture and ask for each key **using it as the reference** (same character, same position, same size, only the expression changes):

   | Key file | Prompt ending |
   |---|---|
   | `neutral` | calm, eyes open, mouth closed |
   | `blink` | same, both eyes closed |
   | `happy` | big happy smile, eyes curved |
   | `sad` | sad eyebrows, small frown, one tear |
   | `thinking` | eyes looking up and to the side, mouth small |
   | `sleeping` | eyes closed peacefully, mouth relaxed |
   | `pet` | blushing, eyes happy-closed, small hearts |
   | `shake` | dizzy swirly eyes, wobbly mouth |
   | `speaking_open` | mouth open as if saying "ah" |
   | `listening` | eyebrows up, attentive, mouth closed |
   | `look_right` | eyes looking to the right, head still |

   Then the 7 mouth shapes (closed, half, open, "eh", "oh", "oo", "ee", plus smile) on the neutral face, and again on sad, thinking and pet.

3. Tips: say "same character, same framing, only change the face" every time. Ask for no gradients and no shadows. Reject any picture where the head moved.

**Please only use art you made or have the right to share.** Owners can add their own art to their own robot; keep the public repo to art that is yours or openly licensed.

## Turn pictures into firmware

### Option 1: draw with code (easiest to keep consistent)

Copy `faces/starter/make_starter_faces.py`, change the drawing functions for your character, and run:

```
pip install pillow
```
```
python3 faces/starter/make_starter_faces.py out
```

It writes `out/<face>/gifs/` (12 GIFs and `_emote_aliases.json`), `hg_mouth_frames.h`, `hg_face_layout.h`, `sheet.png` and `preview.png`.

### Option 2: AI key pictures

1. Put the keys in `keys/` as `<name>.png` (1024 x 1024, about 16 px border).
2. Run the sprite scripts:
   ```
   python3 sprite-scripts/build.py
   ```
   `build.py` scales the keys into a 240 x 240 face centred on the 320 x 240 screen, builds every animation from them (blink, bounce, hearts, Z z z, envelope) and writes `gifs/`, `sprite_sheet.png` and `sprite_sheet.json`. `shift.py` makes `look_left` from `look_right`; `align.py` reports how far each key's head moved so you can fix it (the `SHIFTS` table in `build.py`). The patch boxes in `build.py` (for example the mouth patch) were made for one face, so check them against your art and adjust.
3. Make 320 x 240 mouth pictures named `<mood>_<viseme>.png` (for example `neutral_open.png`, `sad_round_o.png`) and build the header:
   ```
   python3 faces/starter/make_mouth_header.py mouths 128 140 64 36 > hg_mouth_frames.h
   ```
   Use your own mouth box numbers. Outside the box, each picture must match frame 0 of that mood's GIF.
4. Write `hg_face_layout.h` (copy a starter one and change the numbers).

### Build and flash

1. Copy your 12 GIFs and `_emote_aliases.json` into the firmware tree's `fairy-assets/`.
2. Copy `hg_mouth_frames.h` and `hg_face_layout.h` into `main/boards/m5stack/core-s3/`.
3. Delete the old assets image so it is rebuilt (the build does not notice changed GIFs): `rm -f build/generated_assets.bin`, then `idf.py build`. Both the app (`xiaozhi.bin`) and the assets (`generated_assets.bin`) change, because the mouths are in the app and the GIFs are in the assets.
4. Flash both (v3.6 layout, see [VERSIONS.md](../VERSIONS.md)):
   ```
   esptool --chip esp32s3 -p PORT write-flash 0x20000 build/xiaozhi.bin 0xb00000 build/generated_assets.bin
   ```

## Switching between the starter faces

Each public release has one download per face: `robot`, `boy`, `girl`. Pick one and flash its full image (first time) or its app and assets (already on v3.6+). The face is chosen at build time, so switching means flashing the other face's app and assets. Your Wi-Fi and settings stay.
