# email-meme: email a saved meme

## What it does

"Email me that meme." She finds the meme in `~/NousGirl-memes/`, reads the address and title back, and sends it as a JPEG attachment from her own [AgentMail](https://agentmail.to/) inbox only after you say yes. One tool, `email_meme(name, to, confirm)`, always in two steps:

1. `email_meme(name, to)` sends **nothing**. It returns the address and title to read back, and remembers that read-back for 5 minutes.
2. `email_meme(name, to, confirm=true)` sends, only if it matches a read-back from the last 5 minutes. Each read-back is good for one send.

Safety in code: only addresses on your allow-list (0 approved: it refuses; 1: `to` can be left out; 2+: you must name one), 10 sends per rolling hour, attachments up to 4 MB, a one-line log per send in `~/NousGirl-memes/email-log.txt` (no key, no message body). The API key is read from the profile `.env` at call time, kept in memory only and scrubbed from errors.

## Requirements

- [agentmail](../agentmail/) set up: an AgentMail account and inbox of your own, `AGENTMAIL_API_KEY` in the profile `.env`.
- [local-memes](../local-memes/) (or any meme saved in `~/NousGirl-memes/`).
- Hermes, uv.

## Install

```
./install-addon.sh email-meme apply
mkdir -p ~/.config/nousgirl
cp addons/email-meme/allowlist.example.txt ~/.config/nousgirl/email-meme-allowlist.txt   # then edit it
```

Add [config-snippet.yaml](./config-snippet.yaml) to the profile `config.yaml` and [soul.md](./soul.md) to SOUL.md, then restart the gateway.

## Verify

Offline tests against a mock AgentMail server (nothing is really sent):

```
uv run --no-project --python 3.12 --with "mcp>=1.2,<2" --with pillow python addons/email-meme/tests/test_emailmeme.py
```

It ends with `EMAIL MEME TESTS ALL PASS`. Then say "email me the latest meme": she reads the address back and waits for your yes.

## Rollback

```
./install-addon.sh email-meme rollback
```

Remove the config block and SOUL.md lines, restart the gateway. Delete the allow-list file if you no longer need it.
