# What runs where: Windows, Linux and macOS

Which computer you can use for each part of Nous-Girl: flashing the robot, building firmware, running the voice server, and running Hermes Agent.

> **Read this first.** The maintainer has only tested Nous-Girl on **macOS (Apple Silicon)**. The Windows and Linux steps below come from the upstream projects' own docs (linked next to each step). They are **beta**: they should work, but nobody has confirmed them for Nous-Girl yet. If you try them, please [report what happened](https://github.com/dax8it/nous-girl-stackchan/issues), good or bad.

## Easiest path per computer

| Your computer | Flash the robot | Voice server | Hermes Agent | Notes |
|---|---|---|---|---|
| **Mac, Apple Silicon** (tested) | `esptool` in Terminal | Docker Desktop | `install.sh` or Hermes Desktop | Fastest local speech-to-text (MLX). This is the maintainer's setup. |
| Mac, Intel | `esptool` | Docker Desktop | **not supported** by Hermes on Intel Macs | Use Ollama only (setup C), or run Hermes on another computer. |
| **Windows 10 / 11** | `esptool` in PowerShell, or the browser flasher | Docker Desktop (WSL 2) | native `install.ps1`, or inside WSL2 | Simplest: everything native on Windows, Docker Desktop for the server. |
| **Linux** (Ubuntu or Debian) | `esptool` | Docker Engine | `install.sh` | Add yourself to the `dialout` group for USB. |
| Any computer, server elsewhere | flash from any of the above | a home server or a cloud VPS | same machine as the voice server | Keep the robot traffic private (see [SELF-HOSTING.md](./SELF-HOSTING.md#b-anywhere-your-own-domain-and-a-tunnel-optional)). |

## Flash the robot

Flashing writes a firmware file to the robot over a USB-C cable. The exact files and addresses are in [VERSIONS.md](../VERSIONS.md#how-to-flash); only the **port name** changes per computer.

### Option 1: esptool (all computers)

1. Install Python 3, then esptool ([esptool docs](https://docs.espressif.com/projects/esptool/en/latest/)):
   ```
   pip install esptool
   ```
   On a Mac you can also use `brew install esptool`.
2. Plug in the robot with a **data** USB-C cable (some cables only charge).
3. Find the port:

| Computer | Port looks like | How to find it |
|---|---|---|
| macOS | `/dev/cu.usbmodem1101` | `ls /dev/cu.usbmodem*` |
| Linux | `/dev/ttyACM0` | `ls /dev/ttyACM*` |
| Windows | `COM3`, `COM4`, ... | Device Manager > **Ports (COM & LPT)**. Unplug and replug to see which one appears. |

4. Run the command from [VERSIONS.md](../VERSIONS.md#how-to-flash) with your port in place of `PORT`, for example `-p COM4` or `-p /dev/ttyACM0`. Close any serial monitor first.

**USB drivers.** The CoreS3's ESP32-S3 uses its built-in USB serial, so there is no separate USB chip driver ([Espressif: USB-Serial-JTAG](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_serial_jtag.html)):

- **macOS and Linux:** no driver needed.
- **Linux:** give yourself access to the port once, then log out and back in ([ESP-IDF serial guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/establish-serial-connection.html)):
  ```
  sudo usermod -a -G dialout $USER
  ```
  (On Arch Linux the group is `uucp`.)
- **Windows 10 and 11:** the driver usually installs by itself when you're online. If Device Manager shows two **"USB JTAG/serial debug unit"** devices and no COM port, follow [Espressif's driver steps](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-usb-driver.html).

**Robot not found?** Put the CoreS3 in download mode by hand: with USB plugged in, hold the **reset** button for about 2 to 3 seconds until the green light turns on, then let go ([M5Stack CoreS3 docs](https://docs.m5stack.com/en/core/CoreS3)). Run the flash command again.

### Option 2: in the browser (no install)

[ESP Tool (esptool-js)](https://espressif.github.io/esptool-js/) from Espressif flashes from a web page using Web Serial. It works in **Chrome or Edge** (version 89 or later) on desktop; Safari doesn't support Web Serial ([esptool-js](https://github.com/espressif/esptool-js)).

1. Download the release file you want from [Releases](https://github.com/dax8it/nous-girl-stackchan/releases), and check its checksum ([VERSIONS.md](../VERSIONS.md#where-the-binaries-are)).
2. Open [espressif.github.io/esptool-js](https://espressif.github.io/esptool-js/) in Chrome or Edge.
3. Set Baudrate to 460800 and tap **Connect**. Pick the robot's port.
4. **First install** (wipes Wi-Fi): tap **Erase Flash**, then add the `...-full-0x0.bin` file at address `0x0`.
   **Update** (keeps Wi-Fi): add the `...-app.bin` at `0x20000` and the `...-assets.bin` at `0xB00000`.
5. Tap **Program** and wait. Then press reset on the robot.

[ESP Web Tools](https://esphome.github.io/esp-web-tools/) (one-button install pages) needs a hosted project page. Nous-Girl doesn't have one yet.

### Back up first

Before your first flash, save your robot's original firmware (works on every computer, about 16 MB):

```
esptool --chip esp32s3 -p PORT -b 460800 read-flash 0x0 0x1000000 my-stackchan-backup.bin
```

## Build the firmware yourself

Most people don't need this: use the release files. The patched firmware source is not public yet ([RELEASING.md](./RELEASING.md)); this is for when it is, or for upstream builds. All Nous-Girl builds used **ESP-IDF 6.1.0** ([TOOLCHAIN-VERSIONS.md](./TOOLCHAIN-VERSIONS.md)).

| Computer | How to install ESP-IDF 6.1 |
|---|---|
| macOS (tested) | `git clone -b v6.1 --recursive https://github.com/espressif/esp-idf` then `./install.sh esp32s3` ([Linux and macOS setup](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/linux-macos-setup.html)) |
| Linux | same as macOS, after the system packages in the [Linux setup guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/linux-macos-setup.html) |
| Windows | the **ESP-IDF Installation Manager (EIM)**: `winget install Espressif.EIM`, then install v6.1 and use **Open IDF Terminal** ([Windows setup](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/windows-setup.html)) |
| Windows with WSL2 | follow the Linux steps inside WSL. USB devices are not visible inside WSL by default; either pass the robot through with usbipd-win ([Microsoft: connect USB devices](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)), or build in WSL and flash from Windows with esptool and the COM port. |

ESP-IDF doesn't allow spaces in the project or ESP-IDF path, on any computer. The face scripts in [sprite-scripts/](../sprite-scripts/) and [faces/starter/](../faces/starter/) are plain Python and run anywhere.

## Run the voice server

The voice server is [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) in Docker ([deployment guide](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/Deployment.md)). Setup steps: [SELF-HOSTING.md](./SELF-HOSTING.md).

| Where | Use | Notes |
|---|---|---|
| macOS (tested) | [Docker Desktop for Mac](https://docs.docker.com/desktop/setup/install/mac-install/) | `host.docker.internal` reaches Hermes and Ollama on the Mac. |
| Windows | [Docker Desktop for Windows](https://docs.docker.com/desktop/setup/install/windows-install/) (WSL 2 backend) | `host.docker.internal` works. If the robot can't connect, check that Windows Firewall allows ports 8000 and 8003 on **private** networks. |
| Linux | [Docker Engine](https://docs.docker.com/engine/install/) | `host.docker.internal` is not there by default. Add `extra_hosts: ["host.docker.internal:host-gateway"]` to the server's service in `docker-compose.yml`. |
| Home server or cloud VPS | Docker Engine on Linux | The robot must reach it: same home network, or a tunnel with access control. Never open ports 8000 or 8003 to the internet. |

### Speech-to-text and voice: what needs which computer

| Part | Mac (Apple Silicon) | Windows / Linux |
|---|---|---|
| Speech-to-text, Parakeet or Whisper on **MLX** (maintainer's setup) | yes, fast on the Mac GPU | **no**: [MLX](https://github.com/ml-explore/mlx) is for Apple silicon only |
| Speech-to-text built into the server (FunASR SenseVoiceSmall, sherpa-onnx, Vosk, or a cloud provider) | yes | yes, these are the server's own options (untested by us). SenseVoiceSmall on CPU uses about 3 GB of RAM. |
| Her voice, Phonon through [ptts-openai-server](https://github.com/gradium-ai/xn-ptts) | yes (tested) | check that project for support; untested by us |
| Her voice, a TTS provider built into the server | yes | yes |
| Brain or vision with [Ollama](https://ollama.com/download) | yes | yes (Ollama has Windows and Linux builds; an NVIDIA GPU helps a lot for `gemma4:e4b`) |

The wake word always runs on the robot, so it doesn't depend on your computer.

## Run Hermes Agent

From the Hermes docs ([Platform Support](https://hermes-agent.nousresearch.com/docs/getting-started/platform-support), [Installation](https://hermes-agent.nousresearch.com/docs/getting-started/installation)), these are Tier 1 platforms:

| Platform | Install | Notes |
|---|---|---|
| macOS, Apple Silicon (tested) | `curl -fsSL https://hermes-agent.nousresearch.com/install.sh \| bash`, or Hermes Desktop | Intel Macs are **not** supported. |
| Windows 10 / 11, native | in PowerShell: `iex (irm https://hermes-agent.nousresearch.com/install.ps1)`, or the MSIX desktop package | No admin rights needed. MCP servers and the gateway run natively ([Windows native guide](https://hermes-agent.nousresearch.com/docs/user-guide/windows-native)). |
| Windows with WSL2 | the Linux `install.sh` inside Ubuntu on WSL2 | Not WSL1. Turn on systemd for the gateway ([WSL2 guide](https://hermes-agent.nousresearch.com/docs/user-guide/windows-wsl-quickstart)). |
| Linux | `install.sh` | Tested on recent Ubuntu. |
| Docker | `docker pull` | Update by running a new image. |

Then follow [HERMES.md](./HERMES.md). On Windows the profile files live under `%LOCALAPPDATA%\hermes` (native) or `~/.hermes` (WSL2).

**Same computer or another one?** Run Hermes on the **same computer as the voice server** if you can. Its API server then stays on `127.0.0.1`, and Docker reaches it through `host.docker.internal`. If Hermes has to run on a different computer, keep both on a private network you control (for example a VPN), use a long random API key, and never expose the Hermes API port to the internet.

**Hosted by Nous Research.** [Hermes Cloud](https://portal.nousresearch.com/cloud) runs an always-on Hermes Agent for you on Nous Research's servers, billed from your [Nous Portal](https://portal.nousresearch.com/) credit or included with a subscription. Nous-Girl has **not** been tested with a cloud agent yet; it's on the Coming soon list in the [README](../README.md#coming-soon-not-released-yet).

## Help us fill this in

Got Nous-Girl working on Windows or Linux? Please [open an issue](https://github.com/dax8it/nous-girl-stackchan/issues) with your OS, what you ran and what happened, so these steps can lose their beta label.
