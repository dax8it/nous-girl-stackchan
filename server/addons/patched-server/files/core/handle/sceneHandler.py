"""Nous-Girl Actor Mode (was "scene mode", 2026-10-25). Server-only, no firmware change.

She performs a plain-text scene file word for word. Scene lines never go to the
LLM (no Hermes brain, no filler). Her lines are synthesized with her current TTS
(PhononML, per-sentence language) and shown on screen through the normal
sanitized display path.

Triggers (checked first in handle_user_intent):
  "action mode" / "actor mode"   she lists her scripts by number and waits ~20 s for a pick
                                 (number or name word; "never mind" cancels; 1 script -> starts it)
  "action mode <choice>"         picks directly ("action mode two", "actor mode solo")
  "what scripts do you have" / "list scripts"   lists them (and waits for a pick)
  "start scene <name>"   (alias) fuzzy match on scene file names
  "read the script"      (alias) script.txt if present, else the lowest-numbered scene (01-...)
  Numbering: number = filename prefix (03-foo is three even if 02 is missing); files without a
  prefix get the next numbers after the highest prefix, alphabetically; README.md and
  hello-test.txt are not in the menu.
  She opens with "Action!" and "stop" ends with "That's a wrap!".
JSON scripts (.json, see parse_json_scene): when a .txt and a .json share a number prefix
(or a file stem) only the .json is listed and played.
  in a scene: say your line (loose match), "next line" to skip ahead,
  "stop" / "stop scene" to end. Auto-exits after ~3 min with no input.

Scene files live in data/scenes inside the container (data/ is a volume
mount in docker-compose, so you can edit them on the host).
Undo: remove this file and the hook in intentHandler.py.
"""
import asyncio
import difflib
import json
import os
import re
import time
import uuid

TAG = __name__

SCENE_DIRS = ["data/scenes", "scripts"]
SCENE_EXTS = (".txt", ".scene", ".md", ".json")
IDLE_TIMEOUT_S = 180
PICK_TIMEOUT_S = 20       # Actor Mode menu: how long she waits for a pick after she stops talking
MISS_LIMIT = 2            # after this many unmatched replies, accept and continue
MATCH_THRESHOLD = 0.5
FRAME_S = 0.06            # opus frame length the server sends
HER_LABELS = {"NOUS", "NOUS-GIRL", "NOUS GIRL", "NOUSGIRL", "HER", "ROBOT", "NG"}

# ── parsing ──────────────────────────────────────────────────────────────────
_CUE_RE = re.compile(r"^\[\s*([A-Za-z]+)\s*(?::\s*(.*?)|\s+(.*?))?\s*\]$")
_SPK_RE = re.compile(r"^([A-Z][A-Z0-9 _-]{0,20}?)\s*(?:\[([a-z]{2})\])?\s*:\s*(.*)$")
_SENT_RE = re.compile(r"(?<=[.!?])\s+")


def parse_scene(text):
    """-> list of items: {"t": "her", "text", "lang"} | {"t": "human", "who", "text"}
    | {"t": "cue", "kind", "arg"}. '#' lines and blank lines are ignored."""
    items = []
    for raw in (text or "").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = _CUE_RE.match(line)
        if m:
            kind = m.group(1).lower()
            arg = (m.group(2) if m.group(2) is not None else m.group(3) or "").strip()
            items.append({"t": "cue", "kind": kind, "arg": arg})
            continue
        m = _SPK_RE.match(line)
        if m and m.group(3).strip():
            who = m.group(1).strip().upper()
            body = m.group(3).strip()
            if who in HER_LABELS:
                items.append({"t": "her", "text": body, "lang": m.group(2)})
            else:
                items.append({"t": "human", "who": who, "text": body})
            continue
        items.append({"t": "her", "text": line, "lang": None})
    return items


def split_sentences(text):
    return [s.strip() for s in _SENT_RE.split(text or "") if s.strip()]


# ── matching ─────────────────────────────────────────────────────────────────
def _norm(s):
    s = (s or "").lower().replace("’", "'")
    s = re.sub(r"[^\w\s']", " ", s)
    return re.sub(r"\s+", " ", s).strip()


_FILLER_WORDS = {"the", "a", "an", "and", "so", "but", "um", "uh", "okay", "ok", "oh", "well",
                 "you", "i", "is", "it", "to", "of", "what", "can", "do"}


def line_match_score(heard, expected):
    """0..1, tolerant of flubs, missing words and small ad-libs."""
    h, e = _norm(heard), _norm(expected)
    if not h or not e:
        return 0.0
    ratio = difflib.SequenceMatcher(None, h, e).ratio()
    ew = [w for w in e.split() if w not in _FILLER_WORDS] or e.split()
    hw = h.split()
    hits = 0
    for w in ew:
        if w in hw or difflib.get_close_matches(w, hw, n=1, cutoff=0.8):
            hits += 1
    cover = hits / len(ew)
    return max(ratio, cover)


def line_matches(heard, expected, threshold=MATCH_THRESHOLD):
    return line_match_score(heard, expected) >= threshold


def _scene_key(filename):
    stem = os.path.splitext(os.path.basename(filename))[0].lower()
    num = re.match(r"^(\d+)", stem)
    words = re.sub(r"^[\d\s_.-]+", "", stem)
    words = re.sub(r"[_\-.]+", " ", words).strip()
    return words, (int(num.group(1)) if num else None)


_NUM_WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7,
              "eight": 8, "nine": 9, "ten": 10}


def match_scene_name(query, filenames):
    """Fuzzy-pick a scene file for a spoken name. None if nothing is close."""
    q = _norm(query)
    q = re.sub(r"^(the|a|my|number)\s+", "", q).strip()
    if not filenames:
        return None
    keys = {f: _scene_key(f) for f in filenames}
    qn = _NUM_WORDS.get(q) if q in _NUM_WORDS else (int(q) if q.isdigit() else None)
    if qn is not None:
        for f, (_, n) in keys.items():
            if n == qn:
                return f
    if not q:
        return None
    best, best_score = None, 0.0
    for f, (words, _) in keys.items():
        if not words:
            continue
        if q == words:
            return f
        score = difflib.SequenceMatcher(None, q, words).ratio()
        if q in words or words in q:
            score = max(score, 0.9)
        qw = q.split()
        ww = words.split()
        if qw and all(any(difflib.SequenceMatcher(None, a, b).ratio() > 0.8 for b in ww) for a in qw):
            score = max(score, 0.85)
        if score > best_score:
            best, best_score = f, score
    return best if best_score >= 0.55 else None


# ── Actor Mode menu: numbering and picking ──────────────────────────────────
MENU_EXCLUDE = {"readme.md", "hello-test.txt"}
MENU_EXCLUDE_STEMS = {"readme", "hello-test"}
_MORE_NUMS = {"eleven": 11, "twelve": 12, "thirteen": 13, "fourteen": 14, "fifteen": 15,
              "sixteen": 16, "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20}
_ORDINALS = {"first": 1, "second": 2, "third": 3, "fourth": 4, "fifth": 5, "sixth": 6,
             "seventh": 7, "eighth": 8, "ninth": 9, "tenth": 10}
_SOUNDALIKE = {"won": 1, "to": 2, "too": 2, "tu": 2, "tree": 3, "free": 3, "for": 4, "fore": 4,
               "ate": 8, "sex": 6}
_WORD_OF = {v: k for k, v in list(_NUM_WORDS.items()) + list(_MORE_NUMS.items())}


def num_word(n):
    return _WORD_OF.get(n, str(n))


def number_scenes(files):
    """Menu numbering -> [(num, file, friendly_name)] sorted by num.
    Number = filename prefix value (03-foo is three even if 02 is missing). Files with no
    numeric prefix get the next numbers after the highest prefix, alphabetically.
    README.md and hello-test.txt are left out of the menu."""
    menu = [f for f in files if f.lower() not in MENU_EXCLUDE
            and os.path.splitext(f)[0].lower() not in MENU_EXCLUDE_STEMS]
    pref = [(_scene_key(f)[1], f) for f in menu if _scene_key(f)[1] is not None]
    rest = sorted((f for f in menu if _scene_key(f)[1] is None), key=lambda f: f.lower())
    top = max([n for n, _ in pref], default=0)
    out = sorted(pref) + [(top + i + 1, f) for i, f in enumerate(rest)]
    return [(n, f, _scene_key(f)[0] or os.path.splitext(f)[0]) for n, f in out]


def _choice_number(q):
    if q.isdigit():
        return int(q)
    for table in (_NUM_WORDS, _MORE_NUMS, _ORDINALS, _SOUNDALIKE):
        if q in table:
            return table[q]
    return None


_CHOICE_LEAD = re.compile(r"^(?:(?:um|uh|ok|okay|so|well|let'?s do|let'?s|do|play|start|pick|choose|"
                          r"i want|i'd like|i choose|i pick|go with|how about|the|a|my|number|no|"
                          r"script|scene|one called|one named|called|named)\s+)+")
_CHOICE_TAIL = re.compile(r"(?:\s+(?:please|thanks|thank you|one|script|scene|then))+$")


def clean_choice(query):
    q = _norm(query)
    q = _CHOICE_LEAD.sub("", q).strip()
    if len(q.split()) > 1:
        q = _CHOICE_TAIL.sub("", q).strip()
    else:
        q = re.sub(r"^(?:please|thanks)$", "", q)
    return q


def resolve_choice(query, files):
    """Spoken pick ("two", "2", "number two", "the solo one", "capabilities") -> file or None.
    Numbers use number_scenes(); names fuzzy-match any file (hello-test too)."""
    q = clean_choice(query)
    if not q or not files:
        return None
    n = _choice_number(q)
    if n is not None:
        for num, f, _ in number_scenes(files):
            if num == n:
                return f
        return None
    return match_scene_name(q, files)


def menu_sentence(menu):
    if len(menu) == 1:
        return ["I have one script: number one, %s." % menu[0][2] if menu[0][0] == 1 else
                "I have one script: number %s, %s." % (num_word(menu[0][0]), menu[0][2]),
                "Say the number to start it, or never mind."]
    parts = "; ".join("%s, %s" % (num_word(n), name) for n, _, name in menu)
    return ["I have %s scripts: %s." % (num_word(len(menu)), parts), "Which one?"]


# triggers
_LEAD = r"^(?:(?:ok(?:ay)?|hey|so|um|uh|please|alright|all right|nous girl|noose girl|new girl)\s+)*"
_START_RE = re.compile(_LEAD + r"(?:let'?s\s+)?(?:start|play|begin|run|do)\s+(?:the\s+|a\s+)?scene(?:\s+(?:called|named))?\s*(?P<name>.*)$")
_READ_RE = re.compile(_LEAD + r"(?:can you\s+|could you\s+)?read\s+(?:the|this|my|a|our)\s+scripts?\b\s*(?P<name>.*)$")
_MODE = (r"(?:action|actions|actor|actors|acting|active|axion)\s+"
         r"(?:modes|mode|moods|mood|mod|mold|made)")
_ACTOR_RE = re.compile(_LEAD + r"(?:(?:start|enter|open|begin|go to|go into|turn on|switch to|"
                       r"let'?s do|let'?s go|let'?s play|play|do)\s+)?(?:the\s+)?" + _MODE
                       + r"\b\s*(?P<name>.*)$")
_LIST_RE = re.compile(_LEAD + r"(?:(?:what|which)\s+(?:scripts?|scenes?)\s+(?:do you have|have you got|"
                      r"are there|can you do|do you know)|(?:list|show me|tell me)\s+(?:the\s+|your\s+|"
                      r"all\s+|my\s+)?(?:scripts|scenes)|(?:what|which)\s+are\s+(?:your|the)\s+"
                      r"(?:scripts|scenes))(?:\s+please)?$")
_CANCEL_RE = re.compile(r"^(?:ok(?:ay)?\s+|oh\s+|um\s+|uh\s+)*(?:never ?mind|cancel|forget it|no|nope|"
                        r"no thanks|no thank you|not now|nothing|none|stop|exit|quit|maybe later|later)"
                        r"(?:\s+(?:then|please|thanks))?$")
_YES_RE = re.compile(r"^(?:yes|yeah|yep|sure|ok|okay|go|do it|start it|yes please)$")
_NEXT_RE = re.compile(r"^(?:ok(?:ay)?\s+)?(?:next line|next|skip|keep going|go on|continue)(?:\s+please)?$")
_STOP_RE = re.compile(r"^(?:ok(?:ay)?\s+)?(?:stop|stop scene|stop the scene|end scene|end the scene|stop script|stop the script|cut|that's a wrap|that is a wrap|stop stop|"
                      r"(?:stop|end|exit|quit|leave)\s+(?:the\s+)?(?:actor|action)\s+(?:mode|mood))(?:\s+please)?$")


def parse_trigger(text):
    """-> ("actor", choice) | ("list", "") | ("start", name) | ("read", name) | None"""
    t = _norm(text)
    m = _ACTOR_RE.match(t)
    if m:
        return ("actor", m.group("name").strip())
    if _LIST_RE.match(t):
        return ("list", "")
    m = _START_RE.match(t)
    if m:
        return ("start", m.group("name").strip())
    m = _READ_RE.match(t)
    if m:
        return ("read", m.group("name").strip())
    return None


def is_next(text):
    return bool(_NEXT_RE.match(_norm(text)))


def is_stop(text):
    return bool(_STOP_RE.match(_norm(text)))


def is_cancel(text):
    return bool(_CANCEL_RE.match(_norm(text)))


# ── scene files ──────────────────────────────────────────────────────────────
def scenes_dir():
    for d in SCENE_DIRS:
        if os.path.isdir(d):
            return d
    return SCENE_DIRS[0]


def dedupe_scenes(files):
    """A .json wins over any non-json script with the same number prefix or the same stem."""
    js = [f for f in files if f.lower().endswith(".json")]
    jnums = {_scene_key(f)[1] for f in js} - {None}
    jstems = {os.path.splitext(f)[0].lower() for f in js}
    out = []
    for f in files:
        if not f.lower().endswith(".json"):
            if os.path.splitext(f)[0].lower() in jstems:
                continue
            if _scene_key(f)[1] is not None and _scene_key(f)[1] in jnums:
                continue
        out.append(f)
    return out


def list_scenes(d=None):
    d = d or scenes_dir()
    try:
        return dedupe_scenes(sorted(f for f in os.listdir(d)
                                    if f.lower().endswith(SCENE_EXTS) and not f.startswith(".")
                                    and f.lower() != "readme.md"
                                    and os.path.isfile(os.path.join(d, f))))
    except FileNotFoundError:
        return []


def pick_scene(kind, name, d=None):
    """No name ('read the script' / 'start scene'): a file named script.txt, else the
    lowest-numbered file (01-...), else the first alphabetically. A name -> match_scene_name."""
    d = d or scenes_dir()
    files = list_scenes(d)
    if not files:
        return None
    if name:
        return resolve_choice(name, files)
    for f in files:
        if _scene_key(f)[0] == "script":
            return f
    numbered = [f for f in files if _scene_key(f)[1] is not None]
    if numbered:
        return min(numbered, key=lambda f: (_scene_key(f)[1], f))   # lowest number: 01-...
    return files[0]


# ── cue mapping onto her real device tools ───────────────────────────────────
FACES = {"neutral", "happy", "sad", "thinking", "sleeping", "pet", "shake",
         "speaking", "listening", "look_left", "look_right"}
FACE_ALIASES = {"love": "pet", "surprised": "shake", "shocked": "shake", "angry": "shake",
                "mad": "shake", "think": "thinking", "sleepy": "sleeping", "sleep": "sleeping",
                "normal": "neutral", "smile": "happy", "cry": "sad", "left": "look_left",
                "right": "look_right"}
COLORS = {"red", "orange", "yellow", "green", "lime", "teal", "cyan", "blue", "navy",
          "purple", "violet", "magenta", "pink", "white", "warm white", "gold", "brown", "gray"}
LIGHT_MOODS = {"happy", "calm", "party", "spooky", "sleepy", "focus"}
DANCES = {"wiggle", "nod", "disco", "robot", "sway"}


HEAD_POSES = {"left": {"yaw": 40, "pitch": 45}, "right": {"yaw": -40, "pitch": 45},
              "up": {"yaw": 0, "pitch": 70}, "down": {"yaw": 0, "pitch": 20}}
HEAD_CENTER = {"center", "centre", "home", "forward", "straight", "middle"}
HEAD_SHAKE = ["left", "right", "left", "center"]      # expanded by the JSON loader, 0.4 s apart


def _num(v, default=None):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def map_cue(kind, arg, params=None):
    """-> (device_tool_wire_name, args, note) or (None, None, why) for unsupported.
    Music/pause are handled by the scene runner, not here."""
    a = (arg or "").strip().lower()
    if kind == "head":
        p = params or {}
        if "yaw" in p or "pitch" in p:
            yaw = int(max(-128, min(128, _num(p.get("yaw"), 0))))
            pitch = int(max(3, min(87, _num(p.get("pitch"), 45))))
            return "self_servo_set_angles", {"yaw": yaw, "pitch": pitch}, ""
        a = a.replace("_", " ")
        if a in HEAD_CENTER or a == "":
            return "self_servo_center", {}, ""
        if a in HEAD_POSES:
            return "self_servo_set_angles", dict(HEAD_POSES[a]), ""
        if a in ("look around", "around", "scan"):
            return "self_servo_look_around", {}, ""
        if a in ("nod", "yes"):
            return "self_dance_preset", {"name": "nod", "loops": 1}, ""
        return None, None, "head '%s' not supported" % arg
    if kind == "face":
        f = a.replace(" ", "_")
        f = FACE_ALIASES.get(f, f)
        if f not in FACES:
            return None, None, "face '%s' not supported" % arg
        note = "" if a.replace(" ", "_") in FACES else "face '%s' shown as '%s'" % (arg, f)
        return "self_face_show", {"emotion": f}, note
    if kind in ("lights", "light", "led", "leds"):
        base = {"color": "", "mood": "", "cycle": "rainbow", "speed": "slow", "brightness": 60}
        if a in ("off", "dark", "none"):
            return "self_led_light", dict(base, action="off"), ""
        if a in ("normal", "auto", "default"):
            return "self_led_light", dict(base, action="normal"), ""
        if a in ("rainbow", "breathe"):
            return "self_led_light", dict(base, action="cycle", cycle=a), ""
        if a in ("rainbow fast", "disco"):
            return "self_led_light", dict(base, action="cycle", cycle="rainbow", speed="fast"), ""
        if a in COLORS or a.replace("_", " ") in COLORS:
            return "self_led_light", dict(base, action="color", color=a.replace("_", " ")), ""
        if re.fullmatch(r"#[0-9a-f]{6}", a):
            return "self_led_light", dict(base, action="color", color=a), ""
        if a in LIGHT_MOODS:
            return "self_led_light", dict(base, action="mood", mood=a), ""
        return None, None, "lights '%s' not supported" % arg
    if kind == "dance":
        if a in ("stop", "off"):
            return "self_dance_stop", {}, ""
        name = a or "disco"
        if name not in DANCES:
            return None, None, "dance '%s' not a preset (%s)" % (arg, ", ".join(sorted(DANCES)))
        return "self_dance_preset", {"name": name, "loops": 2}, ""
    return None, None, "unknown cue [%s]" % kind


_LEAD_NUM = re.compile(r"^[\d\s_.-]+")


def match_music(query, files):
    q = _norm(query)
    if not files:
        return None
    if not q or q in ("random", "any", "something"):
        import random
        return random.choice(files)
    best, score = None, 0.0
    for f in files:
        stem = os.path.splitext(os.path.basename(f))[0].lower()
        words = re.sub(r"[_\-.]+", " ", _LEAD_NUM.sub("", stem)).strip()
        s = difflib.SequenceMatcher(None, q, words).ratio()
        if q in words:
            s = max(s, 0.9)
        if s > score:
            best, score = f, s
    return best if score >= 0.5 else None


# ── JSON scripts ────────────────────────────────────────────────────────────
HER_SPEAKERS = {"nous", "her", "nous-girl", "nous girl", "nousgirl", "nous_girl", "robot", "ng"}
USER_SPEAKERS = {"user", "me", "human", "you", "kid", "guest"}
ACTION_ALIASES = {"light": "lights", "led": "lights", "leds": "lights", "emote": "face",
                  "expression": "face", "emotion": "face", "move": "dance", "body": "dance",
                  "song": "music", "sound": "music", "wait": "pause", "sleep": "pause",
                  "voice": "lang", "language": "lang", "look": "head", "servo": "head"}
_STEP_KEYS = ("value", "duration", "seconds", "shown_as", "yaw", "pitch")


def _speaker(name):
    n = str(name or "").strip().lower()
    if n in HER_SPEAKERS:
        return "NOUS"
    if n in USER_SPEAKERS or not n:
        return "USER"
    return n.upper()


def _as_list(a):
    if a is None:
        return []
    return a if isinstance(a, list) else [a]


def _secs(x):
    x = max(0.0, min(float(x), 60.0))
    return str(int(x)) if x == int(x) else ("%g" % x)


def expand_action(obj, state):
    """One JSON action -> scene items. value may be a list (sequence); each step a string or
    {"value", "duration"}. "duration" adds a hold after each step (music: plays that long).
    "lang"/"voice" changes state["lang"] for the NOUS lines that follow (no item)."""
    if isinstance(obj, str):
        obj = {"action": obj}
    if not isinstance(obj, dict):
        return []
    kind = str(obj.get("action") or obj.get("type") or "").strip().lower()
    kind = ACTION_ALIASES.get(kind, kind)
    if not kind:
        return []
    if kind == "pause":
        secs = _num(obj.get("seconds", obj.get("duration", obj.get("value"))), 1.0)
        return [{"t": "cue", "kind": "pause", "arg": _secs(secs)}]
    if kind == "lang":
        v = str(obj.get("value") or "").strip().lower()
        state["lang"] = None if v in ("", "auto") else v
        return []
    dur0 = _num(obj.get("duration", obj.get("seconds")))
    raw = obj.get("value", "")
    steps = raw if isinstance(raw, list) else [raw]
    if kind == "face" and obj.get("shown_as") and not isinstance(raw, list):
        steps = [obj["shown_as"]]
    out = []
    for stp in steps:
        if isinstance(stp, dict):
            val = stp.get("shown_as") or stp.get("value", "") if kind == "face" else stp.get("value", "")
            dur = _num(stp.get("duration", stp.get("seconds")), dur0)
            params = {k: stp[k] for k in ("yaw", "pitch") if k in stp}
        else:
            val, dur = stp, dur0
            params = {k: obj[k] for k in ("yaw", "pitch") if k in obj}
        val = "" if val is None else str(val).strip()
        if kind == "head" and not params and val.lower() in ("shake", "no"):
            for pose in HEAD_SHAKE:
                out.append({"t": "cue", "kind": "head", "arg": pose})
                out.append({"t": "cue", "kind": "pause", "arg": "0.4"})
        else:
            item = {"t": "cue", "kind": kind, "arg": val}
            if params:
                item["params"] = params
            out.append(item)
        if dur:
            out.append({"t": "cue", "kind": "pause", "arg": _secs(dur)})
    return out


def _attached(e):
    acts = []
    for a in _as_list(e.get("action")) + _as_list(e.get("actions")):
        if isinstance(a, str):
            a = dict({k: e[k] for k in _STEP_KEYS if k in e}, action=a)
        acts.append(a)
    return acts


def parse_json_scene(text):
    """JSON script -> the same items parse_scene() gives.
    {"title", "characters", "lines": [ entry, ... ]} (or a bare list of entries). Entries:
      {"speaker": "NOUS"|"her"|"nous", "dialogue": "...", "lang": "en"|"auto", "action": ...}
          she says it verbatim; attached actions fire BEFORE she speaks
      {"speaker": "USER"|"me"|..., "dialogue": "...", "action": ...}
          his cue line; she waits for it, never speaks it; attached actions fire right after she hears it
      {"action": "lights"|"face"|"dance"|"head"|"music"|"pause"|"lang", "value": ..., "duration": s}
      {"action": [ {...}, {...} ]}       several actions in a row
      {"comment": "..."}                 ignored"""
    doc = json.loads(text)
    entries = doc if isinstance(doc, list) else (doc.get("lines") or doc.get("script") or [])
    state = {"lang": None}
    items = []
    for e in entries:
        if isinstance(e, str):
            items.append({"t": "her", "text": e.strip(), "lang": state["lang"]})
            continue
        if not isinstance(e, dict):
            continue
        if "dialogue" in e or "line" in e:
            line = str(e.get("dialogue", e.get("line")) or "").strip()
            who = _speaker(e.get("speaker", "NOUS"))
            acts = []
            if who == "NOUS":
                for a in _attached(e):
                    acts += expand_action(a, state)
                items += acts
                if line:
                    lang = str(e.get("lang") or "").strip().lower()
                    lang = None if lang in ("", "auto") else lang
                    items.append({"t": "her", "text": line, "lang": lang or state["lang"]})
            else:
                if line:
                    items.append({"t": "human", "who": who, "text": line})
                for a in _attached(e):
                    items += expand_action(a, state)
            continue
        if "action" in e or "actions" in e:
            a = e.get("action", e.get("actions"))
            if isinstance(a, (list, dict)):
                for x in _as_list(a):
                    items += expand_action(x, state)
            else:
                items += expand_action(e, state)
    return items


# ── runtime ─────────────────────────────────────────────────────────────────
class SceneState:
    def __init__(self, name, items):
        self.name = name
        self.items = items
        self.pos = 0
        self.waiting_for = None    # the human item we are waiting on
        self.misses = 0
        self.task = None
        self.last_input = time.time()
        self.music = None          # {"packets": [...], "pos": int, "file": str}
        self.clock = 0.0           # when queued audio should finish playing
        self.sid = None


def _log(conn):
    return conn.logger.bind(tag=TAG)


def _state(conn):
    return getattr(conn, "hg_scene", None)


async def _end_scene(conn, why):
    st = _state(conn)
    if st is None:
        return
    conn.hg_scene = None
    if st.task and not st.task.done() and st.task is not asyncio.current_task():
        st.task.cancel()
    _log(conn).info("scene '%s' ended: %s" % (st.name, why))


async def hg_try_scene(conn, text):
    """Return True if this utterance belongs to scene mode (so no LLM)."""
    try:
        return await _hg_try_scene(conn, text)
    except Exception as e:
        import traceback
        _log(conn).error("scene mode error: %r %s" % (e, traceback.format_exc()))
        return False


async def _hg_try_scene(conn, text):
    cfg = (conn.config.get("plugins", {}) or {}).get("scene_mode", {}) or {}
    if cfg.get("enabled", True) is False:
        return False
    st = _state(conn)
    end_prompt = (conn.config.get("end_prompt", {}) or {}).get("prompt")
    if st is not None:
        if end_prompt and text == end_prompt:
            await _end_scene(conn, "idle close")
            return False
        if time.time() - st.last_input > IDLE_TIMEOUT_S and st.waiting_for is not None:
            await _end_scene(conn, "timeout")
            st = None
    trig = parse_trigger(text)
    if trig is not None:
        conn.hg_actor_pick = None
        if st is not None:
            await _end_scene(conn, "new scene requested")
        return await _start(conn, trig, text)
    pick = getattr(conn, "hg_actor_pick", None)
    if pick is not None and st is None:
        if end_prompt and text == end_prompt:
            conn.hg_actor_pick = None
            return False
        return await _handle_pick(conn, pick, text)
    if st is None:
        return False
    st.last_input = time.time()
    from core.handle.sendAudioHandle import send_display_message
    if is_stop(text):
        await _end_scene(conn, "stop")
        await _say(conn, ["That's a wrap!"], text, "en")
        return True
    if st.waiting_for is None:
        # she is still performing; ignore stray speech (no barge-in on v3.6.6)
        _log(conn).info("scene busy, ignored: %s" % text)
        return True
    expected = st.waiting_for["text"]
    score = line_match_score(text, expected)
    if is_next(text) or score >= MATCH_THRESHOLD or st.misses + 1 >= MISS_LIMIT:
        _log(conn).info("scene cue ok (score %.2f, misses %d): %s" % (score, st.misses, text))
        st.waiting_for = None
        st.misses = 0
        st.task = asyncio.create_task(_perform(conn, st, text))
        return True
    st.misses += 1
    _log(conn).info("scene cue miss %d (score %.2f): heard '%s' want '%s'"
                    % (st.misses, score, text, expected))
    try:
        await send_display_message(conn, "%s: %s" % (st.waiting_for["who"], expected))
    except Exception:
        pass
    return True


async def _start(conn, trig, text):
    kind, name = trig
    d = scenes_dir()
    files = list_scenes(d)
    if kind == "list":
        return await _offer(conn, d, files, text, listing=True)
    if kind == "actor":
        if name and not is_cancel(name):
            f = resolve_choice(name, files)
            if f:
                return await _run_file(conn, d, f, text)
            return await _offer(conn, d, files, text, prefix="I don't have a script called %s." % name)
        return await _offer(conn, d, files, text)
    f = pick_scene(kind, name, d)
    if f is None:
        msg = ("I could not find a script called %s." % name) if name else "I don't have any scripts yet."
        await _say(conn, [msg], text, "en")
        return True
    return await _run_file(conn, d, f, text)


async def _offer(conn, d, files, text, prefix="", listing=False):
    """Say the numbered menu and wait PICK_TIMEOUT_S for a pick. One script: just start it."""
    menu = number_scenes(files)
    if not menu:
        await _say(conn, ([prefix] if prefix else []) + ["I don't have any scripts yet."], text, "en")
        return True
    if len(menu) == 1 and not listing and not prefix:
        return await _run_file(conn, d, menu[0][1], text)
    end = await _say(conn, ([prefix] if prefix else []) + menu_sentence(menu), text, "en")
    conn.hg_actor_pick = {"deadline": max(time.time(), end or 0) + PICK_TIMEOUT_S, "misses": 0,
                          "files": [f for _, f, _ in menu]}
    _log(conn).info("actor mode menu offered (%d scripts)" % len(menu))
    return True


async def _handle_pick(conn, pick, text):
    if time.time() > pick["deadline"]:
        conn.hg_actor_pick = None
        _log(conn).info("actor mode pick expired")
        return False                       # normal chat
    if is_cancel(text):
        conn.hg_actor_pick = None
        await _say(conn, ["Okay, maybe later."], text, "en")
        return True
    d = scenes_dir()
    files = list_scenes(d)
    f = None
    if _YES_RE.match(_norm(text)) and len(pick.get("files") or []) == 1:
        f = pick["files"][0]
    f = f or resolve_choice(text, files)
    if f:
        conn.hg_actor_pick = None
        return await _run_file(conn, d, f, text)
    pick["misses"] += 1
    if pick["misses"] >= 2:
        conn.hg_actor_pick = None
        _log(conn).info("actor mode pick given up: %s" % text)
        return False                       # let normal chat answer
    end = await _say(conn, ["Hmm, I don't have that one. Say a number, or never mind."], text, "en")
    pick["deadline"] = max(time.time(), end or 0) + PICK_TIMEOUT_S
    return True


async def _run_file(conn, d, f, text):
    with open(os.path.join(d, f), encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    if f.lower().endswith(".json"):
        try:
            items = parse_json_scene(raw)
        except Exception as e:
            _log(conn).warning("actor json script %s unreadable: %r" % (f, e))
            await _say(conn, ["That script has a mistake in it."], text, "en")
            return True
    else:
        items = parse_scene(raw)
    if not items:
        await _say(conn, ["That script is empty."], text, "en")
        return True
    items = [{"t": "her", "text": "Action!", "lang": "en"}] + items
    st = SceneState(f, items)
    conn.hg_scene = st
    _log(conn).info("scene '%s' started (%d items) by '%s'" % (f, len(items), text))
    st.task = asyncio.create_task(_perform(conn, st, text))
    return True


async def _tts_packets(conn, sentence, lang):
    """Synthesize one sentence with her current voice. -> opus packet list."""
    from core.utils.util import audio_to_data
    tts = conn.tts
    if lang and hasattr(tts, "last_lang"):
        tts.last_lang = lang       # PhononML keeps last_lang for short/unsure sentences
    loop = asyncio.get_running_loop()
    os.makedirs("tmp", exist_ok=True)
    ext = getattr(tts, "audio_file_type", "wav") or "wav"
    path = os.path.join("tmp", "scene-%s.%s" % (uuid.uuid4().hex[:10], ext))

    def work():
        return asyncio.run(tts.text_to_speak(sentence, None))
    data = await loop.run_in_executor(None, work)
    if not data:
        raise RuntimeError("TTS returned no audio")
    with open(path, "wb") as fh:
        fh.write(data)
    try:
        return await audio_to_data(path, is_opus=True, use_cache=False)
    finally:
        try:
            os.remove(path)
        except OSError:
            pass


def _detect_lang(sentence, forced):
    if forced:
        return forced
    try:
        from core.providers.tts.phonon_ml import detect
    except Exception:
        return None
    lang, confident = detect(sentence)
    if lang and lang != "en":
        return lang
    return "en"


async def _say(conn, sentences, heard, lang):
    """Short system line (not from a scene), straight to TTS."""
    st = SceneState("_say", [{"t": "her", "text": " ".join(sentences), "lang": lang}])
    await _perform(conn, st, heard, register=False)
    return st.clock                        # roughly when she finishes speaking


def _put(conn, item):
    conn.tts.tts_audio_queue.put(item)


def _advance_clock(st, seconds):
    st.clock = max(time.time(), st.clock) + seconds


async def _wait_clock(st):
    delay = st.clock - time.time()
    if delay > 0:
        await asyncio.sleep(delay)


async def _device(conn, wire, args):
    from core.providers.tools.device_mcp.mcp_handler import call_mcp_tool
    import json
    client = getattr(conn, "mcp_client", None)
    if client is None or not client.has_tool(wire):
        _log(conn).info("scene cue skipped, robot has no tool %s" % wire)
        return
    try:
        await call_mcp_tool(conn, client, wire, json.dumps(args), timeout=20)
    except Exception as e:
        _log(conn).info("scene cue %s failed: %r" % (wire, e))


def _groove(conn, on):
    client = getattr(conn, "mcp_client", None)
    if client is not None and client.has_tool("self_dance_groove"):
        asyncio.create_task(_device(conn, "self_dance_groove", {"on": bool(on)}))


async def _music_start(conn, st, arg):
    from plugins_func.functions import play_music as pm
    from core.utils.util import audio_to_data
    pm.initialize_music_handler(conn)
    files = pm.MUSIC_CACHE.get("music_files", [])
    f = match_music(arg, files)
    if not f:
        _log(conn).info("scene music skipped, no song like '%s'" % arg)
        return
    path = os.path.join(pm.MUSIC_CACHE["music_dir"], f)
    packets = await audio_to_data(path, is_opus=True)
    st.music = {"packets": packets, "pos": 0, "file": f}
    _log(conn).info("scene music ready: %s (%d frames)" % (f, len(packets)))
    _groove(conn, True)


def _music_stop(conn, st):
    if st.music is not None:
        _log(conn).info("scene music stopped: %s" % st.music["file"])
        st.music = None
        _groove(conn, False)


async def _pause(conn, st, seconds):
    seconds = max(0.0, min(float(seconds), 60.0))
    if st.music is not None and seconds > 0:
        n = int(seconds / FRAME_S)
        chunk = st.music["packets"][st.music["pos"]:st.music["pos"] + n]
        st.music["pos"] += len(chunk)
        if chunk:
            _put(conn, (_sentence_type().MIDDLE, chunk, None, st.sid))
            _advance_clock(st, len(chunk) * FRAME_S)
        if st.music["pos"] >= len(st.music["packets"]):
            _music_stop(conn, st)
        await _wait_clock(st)
    else:
        await _wait_clock(st)
        await asyncio.sleep(seconds)
        st.clock = time.time()


def _sentence_type():
    from core.providers.tts.dto.dto import SentenceType
    return SentenceType


async def _perform(conn, st, heard, register=True):
    """Perform items from st.pos until the next human line (or the end)."""
    from core.handle.sendAudioHandle import send_stt_message
    ST = _sentence_type()
    try:
        conn.client_abort = False
        st.sid = uuid.uuid4().hex
        conn.sentence_id = st.sid
        await send_stt_message(conn, heard)   # shows what he said, puts her in speaking state
        st.clock = time.time()
        started = False
        while st.pos < len(st.items):
            if register and _state(conn) is not st:
                return
            it = st.items[st.pos]
            st.pos += 1
            if it["t"] == "human":
                st.waiting_for = it
                st.last_input = time.time()
                break
            if it["t"] == "her":
                if st.music is not None:
                    _music_stop(conn, st)
                for sent in split_sentences(it["text"]):
                    lang = _detect_lang(sent, it.get("lang"))
                    try:
                        packets = await _tts_packets(conn, sent, lang)
                    except Exception as e:
                        _log(conn).warning("scene TTS failed for '%s': %r" % (sent, e))
                        continue
                    if register and _state(conn) is not st:
                        return
                    _put(conn, (ST.FIRST, packets, sent, st.sid))
                    started = True
                    _advance_clock(st, len(packets) * FRAME_S)
                continue
            kind, arg = it["kind"], it["arg"]
            if kind == "pause" or kind == "wait":
                try:
                    secs = float(re.sub(r"[^\d.]", "", arg) or 1)
                except ValueError:
                    secs = 1.0
                await _pause(conn, st, secs)
                continue
            if kind in ("music", "song"):
                if arg.lower() in ("stop", "off", "end"):
                    _music_stop(conn, st)
                else:
                    await _music_start(conn, st, arg)
                continue
            wire, args, note = map_cue(kind, arg, it.get("params"))
            if wire is None:
                _log(conn).info("scene cue skipped: %s" % note)
                continue
            if note:
                _log(conn).info("scene cue: %s" % note)
            await _wait_clock(st)            # cue lands after the line before it
            asyncio.create_task(_device(conn, wire, args))
        if st.music is not None:
            _music_stop(conn, st)            # no stray music while she listens
        _put(conn, (ST.LAST, [], None, st.sid))
        if register and st.pos >= len(st.items) and st.waiting_for is None:
            if _state(conn) is st:
                conn.hg_scene = None
                _log(conn).info("scene '%s' finished" % st.name)
        elif register and st.waiting_for is not None:
            _log(conn).info("scene waiting for %s: %s" % (st.waiting_for["who"], st.waiting_for["text"]))
            asyncio.create_task(_watchdog(conn, st))
    except asyncio.CancelledError:
        raise
    except Exception as e:
        import traceback
        _log(conn).error("scene perform error: %r %s" % (e, traceback.format_exc()))
        try:
            _put(conn, (ST.LAST, [], None, st.sid))
        except Exception:
            pass
        if register and _state(conn) is st:
            conn.hg_scene = None


async def _watchdog(conn, st):
    while _state(conn) is st:
        await asyncio.sleep(15)
        if _state(conn) is st and st.waiting_for is not None and \
                time.time() - st.last_input > IDLE_TIMEOUT_S:
            await _end_scene(conn, "timeout (%ds idle)" % IDLE_TIMEOUT_S)
            return
