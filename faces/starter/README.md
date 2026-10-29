# Starter faces (CC0)

Three original faces drawn by `make_starter_faces.py`: **robot** (default in public builds), **boy**, **girl**. Each has all 12 animations, the blink, and 8 lip-sync mouths for the neutral, sad, thinking and pet moods.

| Face | Preview |
|---|---|
| robot | ![robot](robot/preview.png) |
| boy | ![boy](boy/preview.png) |
| girl | ![girl](girl/preview.png) |

Rebuild everything (needs Python 3 and Pillow):

```
python3 make_starter_faces.py .
```

Each folder holds `gifs/` (copy into the firmware's `fairy-assets/`), `hg_mouth_frames.h` and `hg_face_layout.h` (copy into `main/boards/m5stack/core-s3/`), `sheet.png` (every frame, one row per animation, last row the lip-sync mouths) and `preview.png`.

Make your own: see [docs/SPRITES.md](../../docs/SPRITES.md). License: [CC0](LICENSE).
