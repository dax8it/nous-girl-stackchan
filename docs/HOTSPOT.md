# Take her out: use a phone hotspot

The robot can join your phone's hotspot like any other Wi-Fi. Two things matter: the hotspot must be **2.4 GHz**, and the robot must still be able to **reach your voice server** from there. Read [the server part](#the-important-part-can-she-reach-your-server) before you leave the house.

## Contents

- [Why 2.4 GHz](#why-24-ghz)
- [iPhone](#iphone)
- [Android](#android)
- [Add the hotspot on the robot](#add-the-hotspot-on-the-robot)
- [What she does with several networks](#what-she-does-with-several-networks)
- [The important part: can she reach your server?](#the-important-part-can-she-reach-your-server)
- [Battery tips](#battery-tips)
- [Troubleshooting](#troubleshooting)

## Why 2.4 GHz

The robot's chip (ESP32-S3) only has **2.4 GHz** Wi-Fi ([Espressif ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3)). A hotspot that only sends on 5 GHz or 6 GHz is invisible to her.

## iPhone

From [Apple's Personal Hotspot help](https://support.apple.com/en-us/119837) and [Apple's Wi-Fi security guide](https://support.apple.com/guide/security/wi-fi-security-with-apple-devices-secfd166f620/web):

1. Open **Settings** > **Personal Hotspot**. Turn on **Allow Others to Join**.
2. On an iPhone 12 or later, turn on **Maximize Compatibility**. Apple: this turns off 5 GHz (and 6 GHz), uses only 2.4 GHz and WPA2 Personal, which is what the robot needs. It can make the hotspot a bit slower for other devices.
3. **Keep the Personal Hotspot screen open, with the phone unlocked,** while the robot joins. Apple says to stay on that screen until the other device connects. With nobody connected, an iPhone may stop showing its hotspot once you leave that screen, and then the robot can't find it.
4. Keep the name and password simple. The hotspot name is the phone's name (**Settings > General > About > Name**). Short, plain letters and numbers are easiest to type on the robot. The password is shown on the Personal Hotspot screen (8 characters or more).

## Android

Menu names differ by phone maker. On a Google Pixel ([Pixel Phone Help](https://support.google.com/pixelphone/answer/2812516)):

1. Open **Settings** > **Network & internet** > **Hotspot & tethering** > **Wi-Fi hotspot**. Turn on **Wi-Fi hotspot**.
2. Tap the band setting (on many Pixels it is called **Speed & compatibility**) and pick **2.4 GHz**. Some phones show an **Extend compatibility** switch instead, which adds 2.4 GHz. In some countries and on some carriers the band can't be changed.
3. Set the **hotspot name**, **Security** and **Hotspot password**. Keep the name and password simple.
4. Optional: Google notes the hotspot turns off when no devices are connected (**Turn off hotspot automatically**). Turn that off if the robot loses the hotspot while she is idle.

On other phones the names differ: look in the hotspot settings for the band or a compatibility option.

## Add the hotspot on the robot

She stores up to **10** networks, so your home Wi-Fi stays saved.

1. Get the phone ready first ([iPhone](#iphone) or [Android](#android)). Write down the exact hotspot name and password.
2. On the robot: hold the screen 1.5 s > **Wi-Fi** > **Wi-Fi setup mode**. She restarts into her own setup network, **`Xiaozhi-XXXX`**.
3. On **any** phone or computer, join `Xiaozhi-XXXX` (no password). The setup page opens by itself, or go to **`http://192.168.4.1`**.
4. **Type the hotspot name by hand.** An iPhone hotspot usually won't be in the scan list, because the iPhone switches its hotspot off while that same iPhone is joined to the robot's setup network. She matches the name loosely (upper or lower case, extra spaces at the end, curly or straight apostrophes), but spell it exactly to be safe.
5. Type the password (8 to 63 characters, or empty for an open hotspot; anything else is refused and nothing is saved) and tap **Connect**.
6. If she can't test it right now (normal for an iPhone hotspot), she **saves it anyway** and tells you so. Leave the `Xiaozhi-XXXX` network on your phone, open **Settings > Personal Hotspot** and keep that screen open. She joins within about a minute.

While she is in setup mode and nobody is connected to `Xiaozhi-XXXX`, she keeps looking and joins a saved network as soon as it appears (she waits 45 s after setup mode starts and 20 s after the last phone left).

## What she does with several networks

- **At start-up:** she scans and joins the **strongest** saved network she can see. If none answers within 60 seconds, she starts the `Xiaozhi-XXXX` setup network by herself.
- **While connected:** she stays on that network and doesn't scan for others. Coming home doesn't switch her from the hotspot to your home Wi-Fi by itself; she switches when the hotspot goes away (or after **Restart**).
- **When the network drops:** she retries 5 times, then tries her next saved network, then rescans on a timer that starts at 10 seconds and slows down to every 5 minutes.
- To see which networks she knows: Settings > **Wi-Fi** > Saved networks (names only, never passwords). To forget all of them: **Factory reset** in Restart ([SETTINGS.md](./SETTINGS.md#restart)).

## The important part: can she reach your server?

On a hotspot she is on the **internet**, not on your home network. An address like `http://192.168.1.25:8003/xiaozhi/ota/` only works at home, so on the hotspot she will show a network error even though the Wi-Fi works. Pick one of these:

1. **Your own domain with a tunnel (best for going out).** Give your home server a public `https://` name, for example with [Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/), and set the robot's server to `https://robot.example.com/xiaozhi/ota/` (your own name). Then the same address works at home and on any hotspot. Steps and the security checklist: [SELF-HOSTING.md, part B](./SELF-HOSTING.md#b-anywhere-your-own-domain-and-a-tunnel-optional). Put access control in front of it.
2. **A server on a public host or VPS,** reached over `https://` with your own domain and a TLS certificate. Same address everywhere. Don't leave the ports open without access control ([security checklist](./SELF-HOSTING.md#security-checklist)).
3. **A VPN?** Our firmware has no VPN client, so the robot itself cannot join Tailscale or any other VPN. A VPN on your phone or laptop doesn't help her either. If you use Tailscale, the server still needs a public HTTPS address the robot can reach (for example a tunnel as in option 1).
4. **Bring the server with you.** Run the voice server on a laptop that joins the **same hotspot**, and set the robot's server to the laptop's address on that hotspot (find it as in [SELF-HOSTING step 1](./SELF-HOSTING.md#step-1-find-your-server-address)). The address changes from network to network, so you have to update it in Settings > **Server** each time, and some phones keep hotspot devices from talking to each other. If she can't connect, use option 1.
5. **The official xiaozhi cloud.** Set the server to `https://api.tenclass.net/xiaozhi/ota/` ([SETTINGS.md, Server](./SETTINGS.md#server)). Works anywhere with internet, but none of the Nous-Girl add-ons or Hermes features are there.

Any brain she uses (Hermes, Ollama, a cloud model) is reached **by the server**, not by the robot, so it works as long as the server is up.

## Battery tips

- On battery her screen dims by itself after about 5 minutes with nothing happening ([Sleep and power saving](./SETTINGS.md#sleep-and-power-saving)). Lower **Screen** brightness for longer use.
- A hotspot uses a lot of the phone's battery. Google recommends plugging devices in while tethering and turning the hotspot off when you're done ([Pixel Phone Help](https://support.google.com/pixelphone/answer/2812516)). Apple suggests turning Personal Hotspot off when you don't need it ([Apple](https://support.apple.com/en-us/119837)).
- Turn off what you don't need on the go: music, DJ or rave mode and follow faces all keep the robot busy.

## Troubleshooting

| What you see | Try this |
|---|---|
| The hotspot isn't in the setup page list | Normal for an iPhone. Type the name by hand. On Android, check the band is 2.4 GHz. |
| "SAVED, but not tested" | Normal for an iPhone hotspot. Leave `Xiaozhi-XXXX`, open Settings > Personal Hotspot and keep it open. |
| "Password must be 8 to 63 characters" | The password was too short or too long; nothing was saved. |
| Wi-Fi connects, then a server or network error | Her server address only works at home. See [can she reach your server](#the-important-part-can-she-reach-your-server). |
| She doesn't join the iPhone hotspot | Turn on Maximize Compatibility, unlock the phone and keep the Personal Hotspot screen open. Check the name in Saved networks. |
| She stays on the hotspot at home | She doesn't switch while connected. Turn the hotspot off, or Settings > **Restart**. |
