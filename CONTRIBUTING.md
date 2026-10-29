# Contributing

Thanks for helping. This is a small hobby project with one maintainer, so please keep changes small and focused.

## Report a problem or ask a question

- Open an [issue](https://github.com/dax8it/nous-girl-stackchan/issues/new/choose) and pick **Bug report**, **Feature request** or **Question**.
- Include your firmware version (Settings > About) and face.
- **Redact logs first:** no Wi-Fi names or passwords, server addresses, device IDs, tokens, email addresses or personal details.
- Security problems: see [SECURITY.md](./SECURITY.md). Don't post them publicly.

## Send a pull request

1. Fork the repo and make a branch from `main`.
2. Make one change per pull request (a fix, a doc page, a face).
3. Run the safety scan: `python3 .github/scripts/safety_scan.py`
4. Open the pull request and fill in the checklist.

## Safety rules (the scan checks most of these)

- **No secrets:** no API keys, tokens, passwords, `.env` files or server config.
- **No personal details:** no email addresses, no real server addresses or domains, no home paths, no names of family members.
- **No servers baked in:** firmware and docs use placeholders (`http://<your-computer-ip>:8003/xiaozhi/ota/`, `robot.example.com`). Everyone runs their own server.
- **No binaries in git:** firmware goes in GitHub Releases. No flash backups (they hold Wi-Fi passwords), logs, music or model files.
- **Kid-safe:** this robot is used by children. Keep words, images and features suitable for them.

## Art

- Only add art you made yourself or that has an open license that allows sharing and changes (for example CC0 or CC BY). Say which license in the pull request and in a `LICENSE` file next to the art.
- No fan art of characters you don't own, no images of real people, no AI images made to copy a living artist's style.
- The starter faces in `faces/starter/` are CC0. Use them as the template: [docs/SPRITES.md](./docs/SPRITES.md).

## Review

Each pull request gets **one round of review**. After that, the maintainer decides to merge, ask for a specific change, or close it. Please don't be discouraged by a "no": it usually means the change doesn't fit this small project, not that it's bad.

By contributing you agree to follow the [Code of Conduct](./CODE_OF_CONDUCT.md).
