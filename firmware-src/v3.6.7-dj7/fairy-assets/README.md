# dj7 face assets

This directory contains the 12 GIF animations and expression aliases from the archived dj7 source. The historical directory name is `fairy-assets`, but these GIFs are the Nous-Girl tribute face, not the older Fairy character assets.

The GIFs are not covered by the MIT license. Read [ASSET-NOTICE.md](../ASSET-NOTICE.md). Use a CC0 starter face for a publicly distributed build.

The CoreS3 build selects this directory through the `fairy` emoji collection. `scripts/build_default_assets.py` packs the GIFs into `generated_assets.bin`. Mouth pixels and placement are compiled into the app from `main/boards/m5stack/core-s3/hg_mouth_frames.h`.

Follow [BUILD.md](../BUILD.md#connect-the-faces-from-spritesmd) to replace the face. Rebuild and flash matching app and assets when changing art.
