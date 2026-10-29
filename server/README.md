# Server and Hermes add-ons

Many Nous-Girl features live in the voice server or in the Hermes brain, not in the firmware. They are packaged here as **add-ons**: small folders you install one at a time, each with a README that says what it does, what it needs, how to install, verify and roll back.

New here? Get the stock voice server running first ([docs/SELF-HOSTING.md](../docs/SELF-HOSTING.md)), then add [patched-server](./addons/patched-server/), then pick features.

## The add-ons

| Add-on | Side | What it does | Needs firmware |
|---|---|---|---|
| [patched-server](./addons/patched-server/) | server | **Install first.** The core patch: hg bridge for Hermes tools, real next / previous, DJ, radio, rave and Actor Mode code, meme previews, screen text cleanup, OTA lock-down | any |
| [dj-mode](./addons/dj-mode/) | server | DJ set lists with intros and timed cues (example set) | v3.6.7-dj5+ |
| [radio](./addons/radio/) | server | internet radio, 11 public stations | v3.6.7-dj3+ |
| [rave-mode](./addons/rave-mode/) | server + Hermes | "rave mode" by voice (SOUL.md lines) | v3.6.7-dj5+ |
| [actor-mode](./addons/actor-mode/) | server | scripted scenes with cues (two example scripts) | any |
| [multilingual](./addons/multilingual/) | server | Phonon voices in en, es, fr, de, pt, picked per sentence | any |
| [local-stt-mlx](./addons/local-stt-mlx/) | host (Apple Silicon) | optional local speech-to-text | any |
| [stackchan-tools](./addons/stackchan-tools/) | Hermes | 15 robot tools for the brain | any (some tools need newer) |
| [local-memes](./addons/local-memes/) | Hermes | classic memes made on your computer | any |
| [meme-previews](./addons/meme-previews/) | server + Hermes | show a saved meme on her screen | **dj7** |
| [email-meme](./addons/email-meme/) | Hermes | email a meme to approved addresses after a read-back | any |
| [agentmail](./addons/agentmail/) | Hermes | her own email inbox (config template) | any |
| [fun-pack](./addons/fun-pack/) | Hermes | kid games, story time, kid-safe memory | any |
| [hermes-profile](./addons/hermes-profile/) | Hermes | example `.env` with the names the add-ons use | |

## How installing works

[install-addon.sh](./install-addon.sh) copies an add-on's files into your **running** voice server container with `docker cp`, checks the Python syntax inside the container, and runs `docker restart`. It never recreates, rebuilds or removes the container, so your config, volumes and models stay as they are. Hermes-side files go to `~/.nous-girl/hermes/<add-on>/`.

```
cd server
./install-addon.sh list
./install-addon.sh patched-server status      # read only
./install-addon.sh patched-server dry-run     # shows what would change, changes nothing
./install-addon.sh patched-server apply       # backs up, copies, checks, restarts
./install-addon.sh patched-server rollback    # puts the newest backup back
```

- **Backups** go to `~/nous-girl-backups/<add-on>/<timestamp>/` before anything is copied. Files that did not exist before are removed again on rollback.
- **Automatic rollback:** if the syntax check fails, nothing is restarted and the old files are put back. If the server does not answer on its HTTP port within about 90 seconds after the restart, the backup is restored and the container restarted again.
- **Config and SOUL.md stay yours.** The script never edits `data/.config.yaml`, the Hermes `config.yaml` or `SOUL.md`. Each add-on ships a `config-snippet.yaml` and/or `soul.md` for you to merge by hand, and `apply` prints the steps.
- **Settings** (environment variables): `NG_CONTAINER` (default `xiaozhi-esp32-server`, the upstream name), `NG_SERVER_ROOT` (`/opt/xiaozhi-esp32-server`), `NG_ADDONS_DIR` (`~/.nous-girl/hermes`), `NG_BACKUP_DIR` (`~/nous-girl-backups`), `NG_OTA_PORT` (`8003`).
- Runs in bash on macOS, Linux and Windows (WSL 2 or Git Bash with Docker Desktop).

**Recreated the container?** Files copied into a container are lost when it is recreated (for example `docker compose up` after pulling a new image). Run `apply` again, or build your own image from the patched source ([patched-server/UPSTREAM.md](./addons/patched-server/UPSTREAM.md)). [docker-compose.example.yml](./docker-compose.example.yml) shows the volumes the add-ons expect (`data`, `music`) and the `host.docker.internal` line Linux needs.

## Secrets

Nothing in this folder contains a real key, token, address or host name. Every value you must choose is a placeholder like `MAKE_UP_A_LONG_RANDOM_TOKEN` or `YOUR_SERVER_HOST`. Keep the real ones in `data/.config.yaml` and the Hermes profile `.env` ([hermes-profile/.env.example](./addons/hermes-profile/.env.example)), never in git.

## Licenses

- Our code here is MIT ([LICENSE](../LICENSE)).
- [patched-server](./addons/patched-server/) modifies [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) (MIT, [licenses/xiaozhi-esp32-server.txt](../licenses/xiaozhi-esp32-server.txt)) and includes the Fairy console from fairy-stackchan (MIT, [licenses/fairy-stackchan.txt](../licenses/fairy-stackchan.txt)).
- **No model weights, voices, songs or meme pictures are included.** Each README links what to download and its license. Note that the Phonon 40M models are **CC BY-NC 4.0** (non-commercial).
- Radio streams belong to their stations; listen under their terms.
