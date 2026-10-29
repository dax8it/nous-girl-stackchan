# Actor Mode examples

- [hello-show.json](./hello-show.json): a short, kid-safe show that uses faces, lights (one color list), the head, a dance, pauses, a Spanish line and two lines for you.
- [validate_scene.py](./validate_scene.py): checks a script with the real parser from [server/addons/patched-server](../../server/addons/patched-server/) and prints every step. Python 3.8+, no extra packages.

```
python3 examples/actor-mode/validate_scene.py examples/actor-mode/hello-show.json
```

To run the show on the robot, copy it into the server's `data/scenes/` with a number, for example `data/scenes/03-hello-show.json`, and say "action mode three". Full guide: [docs/ACTOR-MODE.md](../../docs/ACTOR-MODE.md).
