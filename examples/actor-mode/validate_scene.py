#!/usr/bin/env python3
"""Check an Actor Mode script offline with the REAL parser from the patched server.

    python3 examples/actor-mode/validate_scene.py examples/actor-mode/hello-show.json [more files...]

It imports core/handle/sceneHandler.py from server/addons/patched-server/files (no robot, no
server, no network), parses the file exactly like the server does, then runs every cue through
the server's own map_cue(). It prints what she would do, step by step, and exits 1 if anything
would be skipped on the robot (an unknown face, color, dance, cue type, ...), the file is empty,
or a JSON key is unknown. Python 3.8+, standard library only. MIT License.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.normpath(os.path.join(HERE, "..", "..", "server", "addons", "patched-server", "files"))
sys.path.insert(0, os.environ.get("NG_SERVER_FILES", SERVER))
from core.handle import sceneHandler as sh  # noqa: E402  (stdlib-only at import time)

LANGS = {"en", "es", "fr", "de", "pt"}          # the multilingual add-on's voices
DOC_KEYS = {"title", "characters", "lines", "script", "comment", "format"}
ENTRY_KEYS = {"speaker", "dialogue", "line", "lang", "action", "actions", "comment",
              "value", "duration", "seconds", "shown_as", "yaw", "pitch", "type"}


def check_keys(raw, problems):
    try:
        doc = json.loads(raw)
    except ValueError as e:
        problems.append("not valid JSON: %s" % e)
        return
    entries = doc if isinstance(doc, list) else None
    if isinstance(doc, dict):
        for k in doc:
            if k not in DOC_KEYS:
                problems.append("unknown top-level key %r (ignored by the server)" % k)
        entries = doc.get("lines") or doc.get("script")
        if not isinstance(entries, list):
            problems.append('no "lines" list')
            return
    if entries is None:
        problems.append("top level must be an object with \"lines\" or a list")
        return

    def walk(e, where):
        if isinstance(e, str):
            return
        if not isinstance(e, dict):
            problems.append("%s: entries must be objects or strings" % where)
            return
        for k in e:
            if k not in ENTRY_KEYS:
                problems.append("%s: unknown key %r (ignored by the server)" % (where, k))
        lang = str(e.get("lang") or "").lower()
        if lang and lang != "auto" and lang not in LANGS:
            problems.append("%s: lang %r has no voice (use %s or auto)" % (where, lang, ", ".join(sorted(LANGS))))
        for a in sh._as_list(e.get("action")) + sh._as_list(e.get("actions")):
            if isinstance(a, dict):
                walk(a, where + " action")
                if str(a.get("action") or a.get("type") or "").lower() in ("lang", "voice", "language"):
                    v = str(a.get("value") or "").lower()
                    if v and v != "auto" and v not in LANGS:
                        problems.append("%s: lang action %r has no voice" % (where, v))
            elif isinstance(a, list):
                for x in a:
                    walk(x, where + " action")
    for i, e in enumerate(entries, 1):
        walk(e, "line %d" % i)


def validate(path):
    raw = open(path, encoding="utf-8").read()
    problems = []
    if path.lower().endswith(".json"):
        check_keys(raw, problems)
        if problems and problems[0].startswith("not valid JSON"):
            return problems, []
        items = sh.parse_json_scene(raw)
    else:
        items = sh.parse_scene(raw)
    if not items:
        problems.append("the script is empty (she would say \"That script is empty.\")")
    steps = []
    for it in items:
        if it["t"] == "her":
            steps.append("SAY  [%s] %s" % (it.get("lang") or "auto", it["text"]))
            if it.get("lang") and it["lang"] not in LANGS:
                problems.append("line %r: lang %r has no voice" % (it["text"][:30], it["lang"]))
        elif it["t"] == "human":
            steps.append("WAIT for %s: %s" % (it["who"], it["text"]))
        else:
            kind, arg = it["kind"], it["arg"]
            if kind in ("pause", "wait"):
                steps.append("PAUSE %s s" % arg)
            elif kind in ("music", "song"):
                steps.append("MUSIC %s (fuzzy match against the server music folder)" % arg)
            else:
                wire, args, note = sh.map_cue(kind, arg, it.get("params"))
                if wire is None:
                    problems.append("cue [%s: %s] would be skipped: %s" % (kind, arg, note))
                    steps.append("SKIP [%s: %s]" % (kind, arg))
                else:
                    steps.append("CUE  %s %s%s" % (wire, json.dumps(args, sort_keys=True),
                                                    ("  (" + note + ")") if note else ""))
    return problems, steps


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    bad = 0
    for p in paths:
        problems, steps = validate(p)
        print("== %s: %d steps" % (p, len(steps)))
        for s in steps:
            print("   " + s)
        for pr in problems:
            print("   PROBLEM: " + pr)
        print("   " + ("OK" if not problems else "%d problem(s)" % len(problems)))
        bad += bool(problems)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
