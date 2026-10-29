# Releasing firmware

Firmware binaries are published as **GitHub Release assets**, never committed to git (`.gitignore` blocks `*.bin`). The repo holds the notes ([VERSIONS.md](../VERSIONS.md)) and the checksums ([firmware/SHA256SUMS](../firmware/SHA256SUMS)).

## Tags

The firmware was built outside this repo, so no commit here matches a build. Each release tag points at the docs commit that first describes it. One annotated tag per published version:

| Tag | Assets | Notes |
|---|---|---|
| `fw-v3.6.7-dj7` | `xiaozhi-v3.6.7-dj7.bin` | **latest**, recommended |
| `fw-v3.6.7-dj6` | `xiaozhi-v3.6.7-dj6.bin` | known meme-preview crash; marked as such |
| `fw-v3.6.7-dj5` | `xiaozhi-v3.6.7-dj5.bin` | fallback |
| `fw-v3.6.7-dj4`, `-dj3`, `-dj2`, `-dj` | app `.bin` each | DJ series history |
| `fw-v3.6.6-ir` | `xiaozhi-v3.6.6-ir.bin` | pre-release, never run on hardware |
| `fw-v3.6.6` ... `fw-v3.6.1` | app `.bin` each | new layout, app-only |
| `fw-v3.6` | merged `headphone-girl-merged-0x0-v3.6.bin`, plus bootloader, partition table, otadata, assets, app | full flash to move to the new layout |
| `fw-v3.5` | `xiaozhi-v3.5.bin` | old layout, app-only on top of v3.4 |
| `fw-v3.4` ... `fw-v3.1` | merged + app `.bin` each | old layout |
| `fw-v3`, `fw-v2`, `fw-v1` | merged `.bin` each | old layout, full flash |

Every release also gets `SHA256SUMS` and the matching section of VERSIONS.md as its notes.

## Never attach

- Flash dumps or backups (they hold Wi-Fi passwords and device settings).
- Source snapshots (`*.tgz`) until they have had their own secrets scan.
- Logs, `.env`, server config, Hermes profile files.
- Music, meme template images, model files.

## Open items before the first public release

1. **Artwork rights.** Every binary includes the face art (GIFs in the assets, mouth frames in the app). Releasing binaries means releasing the art. Confirm it can be shared and under which license.
2. **Server preset.** Builds from v3.2 on include a "Home server" preset pointing at the maintainer's own server address. It holds no key, but it does show that address. Options: publish as is, or rebuild a public dj7 with the preset removed.
3. **Firmware source.** The patch series against upstream is not in the repo yet.
4. **Server and Hermes packs.** Not in the repo yet; each needs a scrub of personal paths, addresses and hostnames first.
5. **Gallery.** Waiting on item 1.
