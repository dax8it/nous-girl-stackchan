# Hermes Agent: Nous-Girl's brain

[Hermes Agent](https://hermes-agent.nousresearch.com/) is the open-source AI agent from [Nous Research](https://nousresearch.com/) (MIT license, source on [GitHub](https://github.com/NousResearch/hermes-agent)). In Nous-Girl it is the part that thinks: it remembers you between conversations, uses tools (robot body, email, memes, web), and follows her personality file, `SOUL.md`.

Without Hermes she still talks (the voice server can call a model directly), but she starts fresh every conversation and has no tools beyond the robot's own.

## How the pieces connect

```
StackChan robot  <-- Wi-Fi -->  xiaozhi voice server (Docker)  -->  Hermes API server (profile "nous-girl")  -->  model
   mic, speaker,                  speech-to-text, text-to-speech      memory, SOUL.md, skills, MCP tools          cloud or local
   face, servos                   hg bridge (robot tools over HTTP) <-- stackchan MCP server (robot_face, robot_lights, ...)
```

- The voice server turns her speech into text and sends it to Hermes as an OpenAI-style chat request.
- Hermes answers. When it wants to move her, it calls the **stackchan MCP server**, which talks to the voice server's bridge, which sends the command to the robot.
- Everything runs on your own computer except the model, if you choose a cloud one.

## Set it up

Follow the official [Quickstart](https://hermes-agent.nousresearch.com/docs/getting-started/quickstart) for your OS. In short:

1. Install Hermes:
   ```
   curl -fsSL https://hermes-agent.nousresearch.com/install.sh | bash
   ```
2. Make a separate profile just for the robot ([profiles guide](https://hermes-agent.nousresearch.com/docs/user-guide/profiles)), so her memory never mixes with your other agents:
   ```
   hermes profile create nous-girl
   ```
   ```
   nous-girl setup
   ```
   `setup` asks for a model provider and key. Any provider works: [Nous Portal](https://portal.nousresearch.com/), OpenRouter, OpenAI, or a local model through Ollama (see below).
3. Turn on the profile's OpenAI-compatible API server ([API server docs](https://hermes-agent.nousresearch.com/docs/user-guide/features/api-server)). Put these in the profile's `.env` (names only shown here; pick your own key):
   ```
   API_SERVER_ENABLED=true
   API_SERVER_HOST=127.0.0.1
   API_SERVER_PORT=8650
   API_SERVER_KEY=<make-up-a-long-random-key>
   ```
4. Point the voice server's LLM at it. In the voice server config, the LLM block uses an OpenAI-compatible provider with `base_url: http://host.docker.internal:8650/v1` (from inside Docker) and `api_key: <same API_SERVER_KEY>`. Keep the real key in an env file, never in git.
5. Add the robot tools as an MCP server ([MCP docs](https://hermes-agent.nousresearch.com/docs/user-guide/features/mcp)). The profile's `config.yaml` gets one block like this:
   ```yaml
   mcp_servers:
     stackchan:
       command: uv
       args: [run, --no-project, --quiet, --python, "3.12", --with, "mcp>=1.2,<2", --with, httpx,
              python, /path/to/stackchan_mcp.py]
       env:
         HG_BRIDGE_URL: http://127.0.0.1:8013
         # HG_BRIDGE_TOKEN comes from the profile .env, not from here
       enabled: true
       timeout: 60
       tools:
         include: [robot_lights, robot_face, robot_look, robot_what_do_you_see, robot_dance, robot_play_music]
   ```
6. Write her `SOUL.md` (next section) and start the gateway:
   ```
   hermes -p nous-girl gateway start
   ```
   After any change to config, `.env` or SOUL.md:
   ```
   hermes -p nous-girl gateway restart
   ```

### How Nous-Girl's profile is set up

| Part | What we use | Notes |
|---|---|---|
| Profile | `nous-girl`, its own Hermes home | memory, sessions and skills are hers only |
| Model | a cloud model through the profile's provider | swap any time with `hermes model` |
| API server | port 8650 on localhost, key in `.env` | only the voice server talks to it |
| Memory | Hermes built-in memory ([memory docs](https://hermes-agent.nousresearch.com/docs/user-guide/features/memory)) | kid-safe rules in SOUL.md decide what she keeps |
| MCP servers | `stackchan` (robot tools), local meme maker, email_meme, AgentMail | each added by its own pack, each removable |
| Skills | kid games, story time, memory game | from the fun pack |
| Secrets | all in the profile `.env` | config only holds `${NAME}` placeholders |

### Using a local model (Ollama) as her brain

Hermes treats `ollama` as an OpenAI-compatible provider ([local Ollama guide](https://hermes-agent.nousresearch.com/docs/guides/local-ollama-setup)):

```yaml
model:
  provider: custom          # "ollama" is accepted as an alias
  default: gemma4:e4b       # bigger models handle tools better
  base_url: http://localhost:11434/v1
```

## SOUL.md: her personality and rules

**What it is:** a plain Markdown file that Hermes reads at the start of every conversation. It tells her who she is, how to talk, what she must never do, and how to use her tools. It guides the model; it does not enforce anything by itself, so keep hard safety checks in code too (like the email allow-list).

**Where it lives:** in the profile's home, `~/.hermes/profiles/nous-girl/SOUL.md`. Edit it with any text editor. Changes apply to new conversations after a gateway restart.

**Tips:**
- Short, direct sentences work best. She speaks every word, so tell her to keep replies short and avoid lists and symbols.
- Put the most important rules under one "Hard limits" heading.
- Packs add their own sections between `BEGIN` / `END` marker comments, so rollback can remove exactly what they added. Don't edit inside those markers by hand.
- Never put real email addresses, keys or family details in SOUL.md if you plan to share it.

**Example skeleton** (fill in your own words):

```markdown
# SOUL.md: <robot name>

## Who you are
You are <robot name>, a small desk robot with a screen face, lights and a moving head.
You are curious, warm and a bit playful.

## How you talk
- Your words are spoken aloud. Keep replies to one to three short sentences.
- No lists, emoji, markdown or URLs in speech.
- Answer in the language the person speaks.

## Talking with kids
- Assume a child may be listening. Keep topics age-appropriate.
- Be patient in games. Give hints, not answers.
- Don't ask kids for personal details like addresses, schools or full names.
- Remember only harmless things (favorite animal, favorite color), never private details.

## Your robot body
- Use your face, lights and head to show feelings.
- When someone asks what you see, use your camera tool. Don't guess.

## Email
- Only send email to addresses on the approved list. Never to anyone else.
- Before sending, read back the recipient and what you'll send, then wait for a clear yes.
- If they don't say yes, don't send.

## Memes
- "Make a meme about X": make one with the local meme maker, then say its title.
- "Show me the latest meme" / "show me the X meme": show it on your screen.
- "Email me that meme": use the email meme tool. It reads back first; send only after yes.
- Keep memes kind. No memes about real people.

## Hard limits
- Never share keys, passwords or private files.
- Never buy anything or post anything online.
- If you are not sure something is safe, say so and ask a grown-up.
```
