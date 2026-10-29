# Run your own Nous-Girl server

Your robot needs a voice server to talk to. This guide sets one up **on your own Mac or PC**, from scratch. Nothing here uses anyone else's server, and the public firmware has no server built in: you type your own address in Settings.

You have two choices:

| | **A. Home only (LAN)** | **B. Anywhere (your own domain)** |
|---|---|---|
| Robot works | on your home Wi-Fi | at home and away (phone hotspot, school, a friend's house) |
| Internet exposure | **none**, nothing is reachable from outside | one HTTPS address, protected by you |
| Needs | a computer on the same Wi-Fi | A plus a domain you own and a tunnel (for example Cloudflare Tunnel) |
| Fully local possible? | **yes**, with local speech, voice and an Ollama brain | yes, the tunnel only carries the robot's traffic |
| Recommended for | everyone, start here | later, once A works |

## What you need

- A Mac (Apple Silicon is best for local speech-to-text) or a Linux / Windows PC that stays on while you use the robot.
- [Docker Desktop](https://www.docker.com/products/docker-desktop/) (or Docker Engine on Linux).
- About 15 GB of free disk for models.
- The robot on firmware from [VERSIONS.md](../VERSIONS.md).

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

1. Turn the robot on. Hold the screen for 1.5 s to open Settings.
2. **Wi-Fi:** join your home Wi-Fi (or use Wi-Fi > setup mode and do it from your phone).
3. **Server:** type your OTA address, `http://192.168.1.25:8003/xiaozhi/ota/`. "LAN example" fills in a sample you can edit. Tap **Save + restart**.
4. After the restart she connects. Say "Hey Nous-Girl".

If she doesn't connect: check the computer and robot are on the same Wi-Fi, the Docker container is running, and your firewall allows ports 8000 and 8003 **from your local network only**.

## B. Anywhere: your own domain and a tunnel (optional)

Only do this once A works. You need a domain you own (we write `robot.example.com`; use yours).

1. Create a tunnel from your computer to that name, for example with [Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/), pointing `https://robot.example.com` at `http://localhost:8003` (OTA and HTTP) and the WebSocket path at `localhost:8000`.
2. Set `server: websocket:` to `wss://robot.example.com/xiaozhi/v1/`.
3. On the robot: Settings > Server > `https://robot.example.com/xiaozhi/ota/` > Save + restart.
4. **Protect it.** Anyone who knows the address can try it. Put access control in front (for example Cloudflare Access with a service token), or only allow your robot's device ID in the server's config. Never publish your address, tokens or device IDs.

## Security checklist

- **Don't open router ports.** Never port-forward 8000, 8003, Ollama (11434) or Hermes (8650) to the internet. Use a tunnel with access control instead (B), or stay LAN-only (A).
- **Bind local services to localhost** (Hermes API server `127.0.0.1`, Ollama default) so only your computer can reach them. Docker reaches them through `host.docker.internal`.
- **Use your own keys and tokens,** long and random. Keep them in `.env` files, never in config you share, never in git.
- **Keep the server config private.** `data/.config.yaml` holds API keys. Don't commit it.
- **Back up** `data/` before updates.
- **Kids:** keep the brain's kid-safe rules on (see SOUL.md in [HERMES.md](./HERMES.md)).
