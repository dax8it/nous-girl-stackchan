# Build v3.6.7-dj7 for StackChan K151

This directory contains the full modified xiaozhi-esp32 source tree, not an overlay. Start here rather than combining the old wake-word files with a new upstream checkout.

## Source and toolchain

The source snapshot is v3.6.7-dj7, based on xiaozhi-esp32 commit `5d54beb743ff49c4e8db81bbef9413bdd6e2ba17`, locally described as `v2.5.0-19-g5d54beb`. Its CMake project version remains `2.5.0`. The fork release label and the app metadata version are different.

The archived tracked source matches fork commit `a11d397d3c8325acc81bd87be6f93d7a6e1c211d`. The snapshot also includes the previously ignored local components, resolved sdkconfig, dependency lock and generated language header needed to build it. The source archive SHA-256 is `8ab50101ebe5b0a9efd00bd89b1fc52b6003b9832cf078188ab857aecd5a382d`.

Use ESP-IDF `v6.1`, reported as `v6.1.0` in the historical binary. The original toolchain record names GCC 15.2.0, Espressif toolchain `esp-15.2.0_20251204`, Python 3.13.5, CMake 4.0.3 and Ninja 1.12.1. See [the toolchain record](../../docs/TOOLCHAIN-VERSIONS.md). IDF 5.x is not supported. Keep `dependencies.lock` and all five local `components/` directories. In particular, the local esp-dl patches are not interchangeable with an unmodified registry download.

Hardware is the M5Stack CoreS3 in StackChan K151, with an ESP32-S3, 16 MB flash and QUAD PSRAM at 80 MHz. Other hardware has not been validated by this build.

## Build the snapshot

Install ESP-IDF v6.1 and its ESP32-S3 tools using [Espressif's instructions](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/). The first build needs access to the Espressif component registry and model/font downloads. Managed dependencies and generated binaries are not committed here.

From the repository root:

```sh
. /path/to/esp-idf/export.sh
idf.py --version
cd firmware-src/v3.6.7-dj7
idf.py -B build -DIDF_TARGET=esp32s3 \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.cores3-k151' \
  -DBOARD_NAME=m5stack-core-s3 build
```

Keep the checked-in `sdkconfig`. It contains the resolved dj7 board, language, wake-word, camera, PSRAM and LVGL settings, with the default server address cleared. `sdkconfig.defaults.cores3-k151` supplies the board-specific overrides. The generic ESP32-S3 defaults select OCT PSRAM, so the CoreS3 QUAD override matters.

Do not start with `idf.py set-target` or the generic `scripts/build.py` command for this snapshot. Those paths can replace the resolved configuration and lose release-specific choices. If your SDK selects the wrong existing Python environment, set `IDF_PYTHON_ENV_PATH` to the environment installed for this SDK and put its `bin` directory first in `PATH` before sourcing `export.sh`.

Successful output includes `build/xiaozhi.bin`, `build/xiaozhi.elf`, `build/generated_assets.bin` and `build/partition_table/partition-table.bin`. The app file is not a full flash image. A reproducible configuration does not promise a byte-identical historical binary: sanitization, build timestamps, paths and host tools can change the bytes.

## Server and privacy settings

There is no private server or Wi-Fi credential bundled in this snapshot. `CONFIG_OTA_URL` and its Kconfig default are empty. Provision your own Wi-Fi and OTA URL using the setup page or Settings > Server, as described in [self-hosting](../../docs/SELF-HOSTING.md).

The Home server button and the dormant meme-image allowlist use `your-server.example.invalid`. That reserved domain is a placeholder, not a service. Replace it with your own host when enabling those features. The Home template ends with an empty `?k=` and cannot be saved until completed or replaced with your server's actual URL. The optional xiaozhi cloud button remains an explicit user choice. Do not commit a completed private URL or token.

The source sanitization also replaces contact emails, protocol-example encryption keys and a historical sprite-workspace path. Copyright notices and license texts remain intact. The dj7 change that leaves `self.screen.show_image` unregistered is preserved.

## Partition layout and app-only flash

The checked-in configuration selects `partitions/v2/16m_hg36.csv`. Do not substitute the stock partition table. The partition table offset is `0x8000`, ota_0 is `0x20000` with size `0x570000`, ota_1 is `0x590000` with the same size, and assets are `0xB00000` with size `0x500000`.

Only use this app-only command on a device already using that layout, with matching assets, and with ota_0 selected for boot. If an OTA update selected ota_1, writing ota_0 alone does not change the active slot. Back up the device first and check [the release flashing instructions](../../VERSIONS.md#how-to-flash).

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 \
  write-flash 0x20000 build/xiaozhi.bin
```

Replace `PORT` with your serial port. This writes only the app at `0x20000`. It does not write the bootloader, partition table, NVS, OTA selection or face assets. Do not use it as a first-install command for a stock robot.

If you change the GIFs or mouth header, the app and assets must match. With the same partition and boot-slot preconditions:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 \
  write-flash 0x20000 build/xiaozhi.bin 0xb00000 build/generated_assets.bin
```

No hardware flash was performed as part of preparing this source publication.

## Connect the faces from SPRITES.md

[SPRITES.md](../../docs/SPRITES.md) describes the 12 GIF animations and mouth shapes. `main/CMakeLists.txt` selects the `fairy` collection for CoreS3. `scripts/build_default_assets.py` packs `fairy-assets/` into `generated_assets.bin`. `stackchan_geometry_display.cc` selects expressions by GIF stem.

The checked-in snapshot preserves the historical tribute GIFs and mouth header. These are the same artwork already present in `faces/tribute/nous-girl/`, not MIT-licensed starter art. Read [the asset notice](ASSET-NOTICE.md) before sharing a build. Publicly distributed builds should use a CC0 starter face or artwork you have the right to distribute.

To use the robot starter face, run these commands from this firmware directory before building:

```sh
cp ../../faces/starter/robot/gifs/*.gif fairy-assets/
cp ../../faces/starter/robot/gifs/_emote_aliases.json fairy-assets/
cp ../../faces/starter/robot/hg_mouth_frames.h main/boards/m5stack/core-s3/
```

Use `boy` or `girl` in place of `robot` for those presets. These commands replace the historical face files in your working copy. Keep a separate checkout if you want to preserve them.

In this exact dj7 snapshot, `hg_mouth_frames.h` contains RGB565 mouth pixels and the `HgMouthSet` coordinates for neutral, sad, thinking and pet. `stackchan_geometry_display.cc` reads those records to place the overlay. Playback audio feeds `hg_lip.cc`, and `main/audio/hg_lipmap.h` selects the mouth shape.

This historical source does not include or read `hg_face_layout.h`. Copying that file alone has no effect. For a different face, adjust the four `scowl_pts_` assignments in `StackChanGeometryDisplay::SetScowl` and the position in `ShowAngerMark` to match the preset's `HG_SCOWL_A0`, `HG_SCOWL_A1`, `HG_SCOWL_B0`, `HG_SCOWL_B1`, `HG_ANGER_X` and `HG_ANGER_Y` values. The unmodified coordinates fit the historical tribute art, not the starter faces.

After changing art, use a new build directory, such as `-B build-robot`, with the same build flags. This avoids reusing a stale `generated_assets.bin`. Flash that directory's app and assets together. A successful compile does not verify visual alignment or physical behavior.

## Verification status

On 2026-11-14, the sanitized source built successfully on macOS with ESP-IDF v6.1, GCC 15.2.0, Python 3.11.3, CMake 4.1.2 and the Unix Makefiles generator with GNU Make 3.81. Ninja was not installed. The build used the command above in a separate source copy. The resolved `sdkconfig` and `dependencies.lock` remained byte-identical to the publication files.

The resulting app is 5,145,264 bytes, within its `0x570000` slot. The generated assets are 4,909,299 bytes, within their `0x500000` partition. The app SHA-256 is `00c874a8b3028c49683a8e2cbf6014672ade278c5e1819d992ac07375eec8aca`. These outputs were not added to the source commit or flashed.

All 81 host build tests pass with `python -m unittest discover -s scripts/tests -v`. One inherited upstream test expected the stock partition filename. Its expectations now name the fork's existing `16m_hg36.csv` default and its inherited target defaults. No partition settings were changed to make the test pass.

This verifies compilation and host tests for the sanitized historical snapshot, not a byte-identical historical binary, starter-face visual alignment or hardware behavior.

## Licenses

The upstream [MIT LICENSE](LICENSE) is preserved byte-for-byte. Local components and imported code retain their own license and attribution files. Root MIT does not cover the tribute artwork or replace third-party model terms. See [ASSET-NOTICE.md](ASSET-NOTICE.md) and [the repository's third-party notices](../../THIRD_PARTY_NOTICES.md).
