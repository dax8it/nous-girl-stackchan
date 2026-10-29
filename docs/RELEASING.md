# Releasing firmware

Firmware binaries are published as **GitHub Release assets**, never committed to git (`.gitignore` blocks `*.bin`). The repo holds the notes ([VERSIONS.md](../VERSIONS.md)) and the checksums ([firmware/SHA256SUMS](../firmware/SHA256SUMS)).

## What gets published

Only **public builds** of the checkpoints. A public build is the same firmware rebuilt with:

- **no server address inside**, not even the upstream default (`CONFIG_OTA_URL` is blank and the xiaozhi.me button is removed). An unconfigured robot connects nowhere, says "No server yet" and opens Settings > Server. The "LAN example" button fills `http://192.168.1.10:8003/xiaozhi/ota/` as a sample to edit. The meme download allow-list only has a placeholder host. See [SELF-HOSTING.md](./SELF-HOSTING.md#first-boot-step-by-step).
- **the CC0 starter faces** (robot, boy, girl) instead of the maintainer's tribute art. See [SPRITES.md](./SPRITES.md).
- Settings > About says `Nous-Girl public <version> (<face> face)`.

Every other version is documented in VERSIONS.md (notes only, no download).

## Beta

Every public release is published as a **pre-release** (`gh release create --prerelease`) and its notes start with:

> **Beta:** builds verified by scan and checksum, not yet tested on hardware. Please [report issues](https://github.com/dax8it/The-one-above-all-stackchan/issues).

v3.6.7-dj7 is still the one the docs recommend.

## Tags

The firmware was built outside this repo, so no commit here matches a build. Each tag points at the docs commit that first describes it.

| Tag | Faces | Notes |
|---|---|---|
| `fw-v3.6.7-dj7` | robot, boy, girl | recommended (beta) |
| `fw-v3.6.7-dj5` | robot | fallback before the meme features |
| `fw-v3.6.6` | robot | last build before DJ mode |
| `fw-v3.6` | robot | first build on the new flash layout |

Not published: v3.4 (old layout; its source tree is not kept, and it uses the older mouth system, so it can't be rebuilt as a public build). Everything else is a step between checkpoints.

## Assets per release

For each face, three files plus checksums:

| File | Flash at | Use |
|---|---|---|
| `nous-girl-<version>-<face>-full-0x0.bin` | `0x0` | first install, from stock firmware or any older layout (wipes Wi-Fi) |
| `nous-girl-<version>-<face>-app.bin` | `0x20000` | update or roll back, keeps Wi-Fi |
| `nous-girl-<version>-<face>-assets.bin` | `0xB00000` | the face GIFs and wake-word models; flash with the app when you switch faces |
| `SHA256SUMS` | | checksums for the files above |

The release notes are the matching section of VERSIONS.md.

## Checks before every release

1. The domain check: `strings` on every `.bin` finds no personal server address, no `api.tenclass.net`, no email address and no home path.
2. The face check: the assets image holds the 12 starter GIFs of the face in its file name.
3. The app fits its slot (`0x570000`).
4. `shasum -a 256 -c SHA256SUMS` passes.

## Never attach

- The maintainer's own builds (tribute art and personal server preset).
- Flash dumps or backups (they hold Wi-Fi passwords and device settings).
- Source snapshots (`*.tgz`) until they have had their own secrets scan.
- Logs, `.env`, server config, Hermes profile files.
- Music, meme template images, model files.

## Still open

1. **Firmware source.** The patch series against upstream is not in the repo yet.
2. **Server and Hermes packs.** Not in the repo yet; each needs a scrub of personal paths, addresses and hostnames first.
3. **Testing.** Public builds have not been flashed on a robot yet (beta). They differ from the tested builds in the faces, the Server page, the blank default server with its "No server yet" screen, and the allow-list host.
