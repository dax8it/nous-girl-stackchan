# fun-pack: games, stories and kid memory

## What it does

- **kid-games** skill: trivia, 20 questions, spelling bee and a memory game, with patient pacing for voice (one item per turn, a short beat before answers, then she waits).
- **story-time** skill: choose-your-own stories with her face, head, lights and dance.
- **Kid memory rules** (SOUL.md): she remembers harmless fun things ("remember I like sharks"), says "I'll remember that" every time, never saves private info, and forgets on request ("forget that").
- **Kid-safe voice toolset** (optional [config-snippet.yaml](./config-snippet.yaml)): the voice path only gets image generation, memory, session search, skills, todo, vision and web, plus your MCP robot tools. No terminal, files, code, browser or cron for voice.

## Requirements

- Hermes Agent with a profile for her ([docs/HERMES.md](../../../docs/HERMES.md)).
- Optional: [stackchan-tools](../stackchan-tools/) so games and stories can use her face, lights and dances.

## Install

```
cp -R addons/fun-pack/skills/kids ~/.hermes/profiles/YOUR_PROFILE/skills/
```

Add [soul.md](./soul.md) to SOUL.md. For kids, also merge [config-snippet.yaml](./config-snippet.yaml) into the profile `config.yaml` (keep a copy of your old `platform_toolsets` line). Then `hermes -p YOUR_PROFILE gateway restart`.

## Verify

"Let's play trivia about space." She asks one question and waits. "Remember I like sharks." She says "I'll remember that", and the line appears in `memories/MEMORY.md` in the profile.

## Rollback

Delete `skills/kids` from the profile, remove the SOUL.md lines, put your old `platform_toolsets` back, restart the gateway.
