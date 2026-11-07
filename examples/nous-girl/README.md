# Nous-Girl personality example

[SOUL.md](./SOUL.md) preserves Nous-Girl's original personality text, with the privacy and optional-feature edits listed below. It keeps the original gaming-style voice, learning topics, full Nous Research section, and kid-safe boundaries. It is a shareable example, not a deployed profile or the newer calm-assistant rewrite.

There are no owner or family details, mailbox addresses, credentials, hostnames, network addresses, ports, or private filesystem paths in this example. Email configuration and email-sending instructions are intentionally omitted.

## Changes from the original

- Removed owner and family references, the named mailbox, both email sections, email permissions, and references to a particular home computer. No private configuration is included.
- Generalized the age description and illustrative memory entry, and removed historical staging and rollback comments.
- Added a tool-availability note so the original feature instructions do not imply that tools are installed. Preserved the original radio, rave, and classic-meme examples.
- Corrected the old saved-meme handoff to use a successful make_meme result, and removed obsolete screen-update and disabled-service notices. No new rendering-speed guarantee is made.
- Added the perspective-taking paragraph approved for this public example. The original personality sections remain intact.

The original identity, speaking rules, learning topics, and complete Nous Research section are otherwise unchanged. Names, template layouts, station lists, and behavior in feature sections describe the original example; check them against your installed add-on version.

## Perspective-taking guidance

The added 'Understanding the person you're talking with' section asks the agent to consider the listener's knowledge and goals, treat inferred thoughts and feelings as uncertain, ask when the distinction matters, and try a different explanation when needed. This is theory-of-mind-inspired interaction design, not a claim that the model can read minds, has human feelings, or reliably understands every listener. Evaluate the behavior in actual conversations.

Uncertainty and misunderstanding repair are core design goals, not optional entertainment features. Try the [conversation checks in the main README](../../README.md#theory-of-mind-uncertainty-and-misunderstanding-repair) with your own model and setup. The examples show what to look for, including adapting explanations, accepting corrections, and tracking different perspectives; they are not claims of completed behavioral tests.

## Use or adapt it

Read the example before using it. Change the name, personality, and language rules to suit your robot. For a calmer assistant, replace the gaming-style paragraph and the playful-response instruction with the tone you prefer.

To use the example as a profile's SOUL.md, keep a private backup of that profile's existing file first. Replacing it changes that profile's identity instructions. This repository example is not loaded automatically, and adding it to the repository does not change a running robot.

See the [Hermes guide](../../docs/HERMES.md) for profile setup. Begin a new conversation after changing the active personality file. Follow the installed Hermes version's instructions for refreshing continuing sessions or restarting the relevant service if needed. This document does not require or perform a restart.

SOUL.md guides the model. It does not install software, connect tools, enforce access controls, or replace safeguards in code. Keep only the optional sections you intend to use, and verify the connected tools before claiming a feature works. Keep private personalization and configuration out of public copies.

## Optional features

| Feature | What it does | What you need |
|---|---|---|
| Games, stories, and harmless memory | Spoken games and stories with turn-by-turn pacing; optional saved preferences | [fun-pack](../../server/addons/fun-pack/README.md), plus configured memory for persistent facts |
| Robot movement and camera | Lights, head movement, dances, and photos on request | [stackchan-tools](../../server/addons/stackchan-tools/README.md) and its documented bridge and device requirements |
| Radio | Plays and changes internet radio stations | [radio](../../server/addons/radio/README.md) |
| Rave mode | Optional music-linked screen colors and movement | [rave-mode](../../server/addons/rave-mode/README.md) and its documented dependencies |
| Classic memes | Places captions on existing templates and saves the result | [local-memes](../../server/addons/local-memes/README.md), including its renderer, font, templates, and MCP configuration |
| Meme display | Briefly shows a saved meme on the robot's screen | [meme-previews](../../server/addons/meme-previews/README.md), its documented firmware requirements, patched bridge, and robot tools |

The meme-generation and display implementations already exist in the linked add-ons. Copying the optional meme instructions alone does not enable either feature. Generation and screen display are separate capabilities; a robot can have one without the other. Follow each add-on's installation, verification, and rollback instructions if you choose to implement it in your own setup.

Template images are not bundled with the meme add-on. Review the source and rights of any images before downloading, redistributing, or publishing them. The example does not grant rights to third-party images or tribute artwork.

No email or meme-email feature is enabled by this example. Do not add a real mailbox address or an approved-recipient list to a public personality file.
