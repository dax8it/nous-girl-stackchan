---
name: story-time
description: Use when a kid asks the robot to tell a story or wants to make up a story together.
---

# Story time

Spoken, choose-your-own stories for kids around 10 to 12, acted out with her face and head.

## Setup (one short turn)
1. Ask for up to three things: a hero, a place, and a problem. If they say "you pick", pick fun ones, or use an interest from memory.
2. Say "Okay, here we go!" and start.

## Telling it
- Tell the story in small chunks: 2 or 3 sentences each, then stop and let them talk.
- BEFORE each chunk, set the scene with the robot tools, then speak:
  - `robot_face`: "happy" for fun parts, "thinking" for mysteries, "sad" for a sad moment, "look_left"/"look_right" for "something moved over there", "shake" for a surprise.
  - `robot_look`: small moves only. Yaw about -30 or 30 to "look" at something, pitch about 30 to look up at a giant or the sky, back to yaw 0 pitch 3 for normal.
  - `robot_lights` mood (optional): "spooky" for a gentle creepy cave, "calm" for night, "party" for a celebration.
- Don't stack more than two tool calls per chunk, and don't move on every sentence. If face following is on, skip `robot_look`.
- Every 2 or 3 chunks, give a choice: "Does the dragon fly to the volcano or the ice castle?" Use what they pick.
- Aim for about 8 to 12 chunks. Wrap up with a happy or satisfying ending.

## Ending
- Finish with `robot_face` "happy", `robot_look` yaw 0 pitch 3, `robot_lights` action "normal".
- Ask if they want another one, once. Don't push.

## Stop
- Stop right away if they say "stop", "the end", "I'm done", or "bedtime". Say a one-line ending and reset face, head and lights.
- A tap on her screen cuts off her voice on the robot itself. If your last chunk got cut off, or their next message is short or off-topic, ask "Want me to keep going?" instead of carrying on.

## Kid-safe rules
- Age 10 to 12: adventure, mystery, funny, a little spooky only if they ask. No gore, no real violence, no grown-up themes, no real people, no brands as villains.
- Bad guys can be beaten with cleverness, teamwork or kindness.
- If the kid steers somewhere unsafe, steer the story back with a fun twist.
- Keep words simple and the pace quick. It's a voice story, so no lists or headings.
