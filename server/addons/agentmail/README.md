# agentmail: her own email inbox

## What it does

She gets her **own** inbox through [AgentMail](https://agentmail.to/) (not yours), so she can read mail and send it when asked: "send an email to Grandma", "check your email". The SOUL.md rules make her read back the recipient and a summary and wait for a yes. This add-on is a config template only: it uses AgentMail's hosted MCP server, nothing runs on your computer.

## Requirements

- Hermes Agent.
- An AgentMail account of your own, one inbox, one API key ([AgentMail docs](https://docs.agentmail.to/)).

## Install

1. In AgentMail, create an inbox and an API key.
2. Add the key to her profile `.env` (for a profile named nous-girl: `~/.hermes/profiles/nous-girl/.env`), never to config or git:
   ```
   AGENTMAIL_API_KEY=YOUR_AGENTMAIL_API_KEY
   ```
3. Add [config-snippet.yaml](./config-snippet.yaml) under `mcp_servers:` in the profile `config.yaml`. Remove tools from `include` that you don't want her to have (for example the draft tools).
4. Add [soul.md](./soul.md) to SOUL.md with your inbox address in place of `YOUR_INBOX_ADDRESS`, and change the "no email" hard limit as it says.
5. `hermes -p <profile> gateway restart`.

`./install-addon.sh agentmail dry-run` prints the same steps.

## Verify

`hermes -p <profile> mcp test agentmail` connects and lists the tools. Then ask her "what's your email address?" and "check your email".

## Rollback

Set `enabled: false` on the `agentmail` block (or remove it), remove the SOUL.md lines and the key from `.env`, restart the gateway. Revoke the key in AgentMail if you don't need it any more.

**Kid-safe note:** the voice path has no approval prompts, so the read-back rule lives in SOUL.md. For email that cannot reach strangers at all, use [email-meme](../email-meme/), which has a hard allow-list in code.
