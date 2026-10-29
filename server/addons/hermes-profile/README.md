# hermes-profile: the .env names

[.env.example](./.env.example) lists every secret the add-ons read, with placeholder values only:

| Name | Used by | Must match |
|---|---|---|
| `API_SERVER_KEY` | Hermes API server, the voice server's LLM block | the `api_key` in the voice server's LLM block |
| `HG_BRIDGE_TOKEN` | [stackchan-tools](../stackchan-tools/) | `hg_bridge.token` in `data/.config.yaml` |
| `AGENTMAIL_API_KEY` | [agentmail](../agentmail/), [email-meme](../email-meme/) | your AgentMail key |
| `HF_TOKEN` | [multilingual](../multilingual/) Phonon servers | your Hugging Face token, after accepting the model terms |

Copy it to the profile folder, fill in your own long random values, and never commit or share the real file. Config files reference these as `${NAME}`.
