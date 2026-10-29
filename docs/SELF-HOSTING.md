# Run your own Nous-Girl server

Your robot needs a voice server to talk to. This guide sets one up **on your own Mac or PC**, from scratch. Nothing here uses anyone else's server, and the public firmware has no server built in: an unconfigured robot connects nowhere until you type an address in Settings.

You have two choices:

| | **A. Home only (LAN)** | **B. Anywhere (your own domain)** |
|---|---|---|
| Robot works | on your home Wi-Fi | at home and away (phone hotspot, school, a friend's house) |
| Internet exposure | **none**, nothing is reachable from outside | one HTTPS address, protected by you |
| Needs | a computer on the same Wi-Fi | A plus a domain you own and a tunnel (for example Cloudflare Tunnel) |
| Fully local possible? | **yes**, with local speech, voice and an Ollama brain | yes, the tunnel only carries the robot's traffic |
| Recommended for | everyone, start here | later, once A works |

## First boot, step by step

Public firmware has **no server built in**. Until you set one, she connects to nothing and opens **Settings > Server** with "No server yet". Do these steps in order.

### Step 1. Find your server address

Pick one:

- **(a) Your own server** (recommended, set up with part A below). The address is:
  ```
  http://<your-computer-ip>:8003/xiaozhi/ota/
  ```
  Replace `<your-computer-ip>` with your computer's address on your home Wi-Fi, for example `http://192.168.1.25:8003/xiaozhi/ota/`. Keep `http://` (not https), the port `:8003`, and the `/` at the end.
- **(a) with your own domain** (part B below): `https://robot.example.com/xiaozhi/ota/` with your domain instead of `robot.example.com`.
- **(b) The official xiaozhi cloud**, run by the upstream xiaozhi team, if you don't want to run a server:
  ```
  https://api.tenclass.net/xiaozhi/ota/
  ```
  This is the default address in the upstream [xiaozhi-esp32 source](https://github.com/78/xiaozhi-esp32/blob/main/main/Kconfig.projbuild). Your voice goes to their cloud, and Nous-Girl's add-ons (Hermes memory, DJ, memes) don't work there.

**How to find your computer's IP** (the computer running the server, on the same Wi-Fi as the robot):

- **Mac:** System Settings > Wi-Fi > Details next to your network > IP address. Or in Terminal: `ipconfig getifaddr en0`
- **Windows:** open Command Prompt, run `ipconfig`, and read "IPv4 Address" under your Wi-Fi adapter.
- **Linux:** `hostname -I` (the first number).

It usually starts with `192.168.` or `10.`. Give the computer a fixed IP in your router so it doesn't change.

### Step 2. Put the robot on your Wi-Fi

The robot needs **2.4 GHz** Wi-Fi (most home routers have it; 5 GHz-only networks won't show).

1. Turn the robot on. With no saved Wi-Fi she starts her own setup hotspot by herself. (Already set up? Hold the screen 1.5 s > **Wi-Fi** > **Wi-Fi setup mode**.)
2. On your phone or computer, open Wi-Fi settings and join the network named **`Xiaozhi-XXXX`** (`XXXX` is 4 letters or numbers, different on every robot). It has no password.
3. A setup page should open by itself. If it doesn't, open a browser and go to **`http://192.168.4.1`**.
4. Pick your home Wi-Fi, type its password, and tap **Connect**.
5. Optional shortcut: on the same page, the **Advanced** tab has **Custom OTA URL**. Paste your address from step 1 there and tap **Save**. Then you can skip step 3.
6. The robot joins your Wi-Fi and restarts. Your phone goes back to your normal Wi-Fi.

Using a phone hotspot instead of home Wi-Fi (iPhone or Android settings, and how she reaches your server from there): [HOTSPOT.md](./HOTSPOT.md).

### Step 3. Enter your server in Settings

1. On the robot, **hold the screen for 1.5 s** to open Settings (with no server set, she opens this page by herself).
2. Tap **Server**. "Server (OTA) address now" says "(none yet ...)" on a new robot.
3. Tap the text box. A keyboard appears. Type your address from step 1 exactly. ("LAN example" fills in `http://192.168.1.10:8003/xiaozhi/ota/` for you to edit.)
4. Tap **Save + restart**. She restarts in a few seconds.

### Step 4. Check that it worked

- **On the robot:** after the restart she plays a short "ready" sound and shows her normal face, with no "No server yet" or error message. Settings > Server shows your address.
- **Say "Hey Nous-Girl".** Her face changes to listening. Ask "what's your name?" and she answers.
- **On your computer** (own server): `docker logs -f xiaozhi-esp32-server` shows a new connection and your words when you speak.
- **Official cloud:** she shows and says a 6-digit code. Log in at [xiaozhi.me](https://xiaozhi.me/), add a device in the console and type the code.

**If it didn't work:**

| You see | Fix |
|---|---|
| "No server yet" again | the address wasn't saved: it must start with `http://` or `https://`. Redo step 3. |
| An error that mentions the server or network | the robot can't reach the computer: same Wi-Fi? computer awake? Docker container running? firewall allows ports 8000 and 8003 on your local network? Is the IP still the same? |
| `Xiaozhi-XXXX` keeps coming back | the Wi-Fi password was wrong or the network is 5 GHz only. Redo step 2. |
| She connects but doesn't answer | check the brain (step 3 of part A) and the server logs. |

## What you need

- A Mac (Apple Silicon is best for local speech-to-text) or a Linux / Windows PC that stays on while you use the robot.
- [Docker Desktop](https://www.docker.com/products/docker-desktop/) (or Docker Engine on Linux).
- About 15 GB of free disk for models.
- The robot on firmware from [VERSIONS.md](../VERSIONS.md).

On Windows or Linux? Differences for Docker, ports and speech models: [PLATFORMS.md](./PLATFORMS.md). Every robot Settings option: [SETTINGS.md](./SETTINGS.md).

## A. Home only (LAN)

### 1. Start the voice server

The server is [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server). Follow its [deployment guide](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/Deployment.md) ("Docker only runs Server"). In short:

```
mkdir -p ~/xiaozhi-server/data ~/xiaozhi-server/models/SenseVoiceSmall
```
```
cd ~/xiaozhi-server
```

Download `docker-compose.yml` and `config.yaml` from the server repo (`main/xiaozhi-server/`), put `docker-compose.yml` in `~/xiaozhi-server`, and save `config.yaml` as `data/.config.yaml`. Download the speech model (`model.pt`) into `models/SenseVoiceSmall` as the guide says. Then:

```
docker compose up -d
```
```
docker logs -f xiaozhi-esp32-server
```

### 2. Find your computer's address

- Mac: System Settings > Wi-Fi > Details, or run `ipconfig getifaddr en0`.
- Linux: `hostname -I`. Windows: `ipconfig`.

Say it's `192.168.1.25`. Then your addresses are:

- OTA (what the robot needs): `http://192.168.1.25:8003/xiaozhi/ota/`
- WebSocket (the server tells the robot this one): `ws://192.168.1.25:8000/xiaozhi/v1/`

In `data/.config.yaml`, set `server: websocket:` to that WebSocket address. Give your computer a fixed IP in your router, or the address can change.

### 3. Pick a brain

In `data/.config.yaml`:

- **Ollama only (simplest, fully local, no memory):** install [Ollama](https://ollama.com/), run `ollama pull gemma4:e4b`, then:
  ```yaml
  selected_module:
    LLM: OllamaLLM
  LLM:
    OllamaLLM:
      type: ollama
      model_name: gemma4:e4b
      base_url: http://host.docker.internal:11434
  ```
- **Hermes Agent (memory, tools, personality):** follow [HERMES.md](./HERMES.md), then point an OpenAI-compatible LLM block at `http://host.docker.internal:8650/v1` with your own Hermes API key.

Restart after config changes:

```
docker compose restart
```

### 4. Connect the robot

Follow [First boot, step by step](#first-boot-step-by-step) with your address from step 2.

### 5. Add Nous-Girl features (optional)

Once she talks to your server, add the patched server and the features you want (DJ mode, radio, Actor Mode, multilingual voice, robot tools for Hermes) from [server/](../server/README.md). Each add-on installs into the running container with `./install-addon.sh <name> apply` and comes out with `rollback`.

## B. Anywhere: your own domain and a tunnel (optional)

Only do this once A works. You need a domain you own (we write `robot.example.com`; use yours).

This is also what lets her work away from home on a phone hotspot ([HOTSPOT.md](./HOTSPOT.md)).

1. Create a tunnel from your computer to that name, for example with [Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/), pointing `https://robot.example.com` at `http://localhost:8003` (OTA and HTTP) and the WebSocket path at `localhost:8000`.
2. Set `server: websocket:` to `wss://robot.example.com/xiaozhi/v1/`.
3. On the robot: Settings > Server > `https://robot.example.com/xiaozhi/ota/` > Save + restart (see [step 3](#step-3-enter-your-server-in-settings)).
4. **Protect it.** Anyone who knows the address can try it. Put access control in front (for example Cloudflare Access with a service token), or only allow your robot's device ID in the server's config. Never publish your address, tokens or device IDs.

## Security checklist

- **Don't open router ports.** Never port-forward 8000, 8003, Ollama (11434) or Hermes (8650) to the internet. Use a tunnel with access control instead (B), or stay LAN-only (A).
- **Bind local services to localhost** (Hermes API server `127.0.0.1`, Ollama default) so only your computer can reach them. Docker reaches them through `host.docker.internal`.
- **Use your own keys and tokens,** long and random. Keep them in `.env` files, never in config you share, never in git.
- **Keep the server config private.** `data/.config.yaml` holds API keys. Don't commit it.
- **Back up** `data/` before updates.
- **Kids:** keep the brain's kid-safe rules on (see SOUL.md in [HERMES.md](./HERMES.md)).
