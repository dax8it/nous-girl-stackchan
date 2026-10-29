"""v3.6.7-dj: DJ set lists for Nous-Girl.

Files: data/setlists/NN-name.json (numbered by filename prefix, like Actor Mode scenes).

  {"title": "Example set", "format": "dj-setlist/1",
   "defaults": {"face": "happy", "lights": "party", "groove": true, "gap": 2,
                "vu": {"on": true, "color": "blue"}},
   "loop": false, "shuffle": false,
   "songs": [
     {"id": "song1", "track": "My-Song", "start": 0, "end": 180,
      "intro": [{"speaker": "NOUS", "dialogue": "Lights down.", "lang": "en"}],
      "outro": [...],
      "cues": [{"at": 30, "action": "face", "value": "scowl", "duration": 2},
               {"at": 45, "action": "vu", "value": "pulse", "duration": 4}]}]}

track   resolved against the server music folder (exact name first, then play_music.py's
        own fuzzy match). No audio inside the file. All audio streams from the server
        through her speech channel (no SD card, no on-device decoding).
start/end  optional seconds into the track.
intro/outro  Actor Mode dialogue entries {speaker, dialogue, lang}; only her lines are spoken.
cues    {at: seconds into the track, action: face|lights|dance|head|pause|voice|vu, value, duration}
        face/lights/dance/head reuse sceneHandler.map_cue (plus face "scowl").
        duration on face/lights/dance/head/vu-color puts the default back afterwards.
        pause: music holds silent for duration. voice: she says value (the song pauses
        while she talks). vu: purple|blue|gold|warm|rainbow|classic|on|off|pulse (+duration).

Voice: "DJ mode" lists the set lists and waits for a pick; "DJ mode <number|name>" starts one;
while a set plays (or is paused by a tap): "next" / "previous" step through songs[] in order,
"continue" resumes, "stop" ends the set, "DJ mode off" ends it and turns DJ mode off.
Actor Mode (sceneHandler.py) is not changed; this module only imports its helpers.
"""
import asyncio
import json
import os
import random
import re
import time
import uuid

from core.handle import sceneHandler as sh

TAG = __name__
SETLIST_DIRS = ["data/setlists"]
FORMAT = "dj-setlist/1"
CUE_ACTIONS = ("face", "lights", "dance", "head", "pause", "voice", "vu")
VU_COLORS = ("purple", "blue", "gold", "warm", "rainbow", "classic")
VU_VALUES = VU_COLORS + ("on", "off", "pulse")
FRAME_S = sh.FRAME_S          # opus frame length (tests may shrink it)
LEAD_S = 1.0                  # how much song audio is queued ahead of the speaker
CHUNK_FRAMES = 8              # 0.48 s per push
PICK_TIMEOUT_S = 20
PAUSED_FORGET_S = 600         # a tapped-away set is forgotten after 10 minutes
MAX_SONGS = 60
_TOP_KEYS = {"title", "format", "defaults", "loop", "shuffle", "songs", "comment"}
_SONG_KEYS = {"id", "track", "start", "end", "intro", "outro", "cues", "comment", "title"}
_CUE_KEYS = {"at", "action", "value", "duration", "lang", "yaw", "pitch", "comment"}
_DEFAULT_KEYS = {"face", "lights", "groove", "gap", "vu"}


class SetlistError(ValueError):
    pass


# ── schema ───────────────────────────────────────────────────────────────────
def _num(v, where, lo=0.0, hi=None):
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise SetlistError("%s must be a number" % where)
    v = float(v)
    if v < lo or (hi is not None and v > hi):
        raise SetlistError("%s must be between %g and %g" % (where, lo, hi if hi is not None else float("inf")))
    return v


def _face_ok(value):
    v = str(value or "").strip().lower()
    if v == "scowl":
        return True
    wire, _, _ = sh.map_cue("face", v)
    return wire is not None


def _lines(entries, where):
    if entries is None:
        return []
    if not isinstance(entries, list):
        raise SetlistError("%s must be a list of {speaker, dialogue, lang}" % where)
    out = []
    for i, e in enumerate(entries):
        w = "%s[%d]" % (where, i)
        if isinstance(e, str):
            e = {"speaker": "NOUS", "dialogue": e}
        if not isinstance(e, dict) or not str(e.get("dialogue") or "").strip():
            raise SetlistError("%s needs a dialogue line" % w)
        if sh._speaker(e.get("speaker", "NOUS")) != "NOUS":
            continue                       # DJ sets never wait for anyone; only her lines
        lang = str(e.get("lang") or "").strip().lower()
        out.append({"text": str(e["dialogue"]).strip(), "lang": None if lang in ("", "auto") else lang})
    return out


def _check_cue_value(action, value, cue, where):
    v = "" if value is None else str(value).strip()
    if action == "face":
        if not _face_ok(v):
            raise SetlistError("%s: face '%s' is not one of her faces" % (where, v))
    elif action in ("lights", "dance"):
        wire, _, why = sh.map_cue(action, v)
        if wire is None:
            raise SetlistError("%s: %s" % (where, why))
    elif action == "head":
        params = {k: cue[k] for k in ("yaw", "pitch") if k in cue}
        wire, _, why = sh.map_cue("head", v, params)
        if wire is None:
            raise SetlistError("%s: %s" % (where, why))
    elif action == "vu":
        if v.lower() not in VU_VALUES:
            raise SetlistError("%s: vu must be one of %s" % (where, ", ".join(VU_VALUES)))
    elif action == "voice":
        if not v:
            raise SetlistError("%s: voice needs the words to say in value" % where)
    elif action == "pause":
        if not cue.get("duration") and not _isnum(value):
            raise SetlistError("%s: pause needs a duration" % where)
    return v


def _isnum(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def expand_cues(cues, defaults, start, end, where="cues"):
    """Validate, then -> list of {"at", "action", "value", ...} sorted by time. A duration on
    face / lights / dance / head / vu-color adds a "put it back" cue at at + duration."""
    if cues is None:
        return []
    if not isinstance(cues, list):
        raise SetlistError("%s must be a list" % where)
    out = []
    for i, c in enumerate(cues):
        w = "%s[%d]" % (where, i)
        if not isinstance(c, dict):
            raise SetlistError("%s must be an object" % w)
        at = _num(c.get("at"), w + ".at", 0.0, 3600.0)
        if at < start:
            raise SetlistError("%s.at %g is before the song's start %g" % (w, at, start))
        if end is not None and at > end:
            raise SetlistError("%s.at %g is after the song's end %g" % (w, at, end))
        action = str(c.get("action") or "").strip().lower()
        if action not in CUE_ACTIONS:
            raise SetlistError("%s.action must be one of %s" % (w, ", ".join(CUE_ACTIONS)))
        dur = None
        if c.get("duration") is not None:
            dur = _num(c.get("duration"), w + ".duration", 0.0, 60.0)
        if action == "pause" and dur is None:
            dur = _num(c.get("value"), w + ".value", 0.0, 60.0) if _isnum(c.get("value")) else None
        value = _check_cue_value(action, c.get("value"), c, w)
        item = {"at": at, "action": action, "value": value, "seq": len(out)}
        if dur is not None:
            item["duration"] = dur
        if c.get("lang"):
            item["lang"] = str(c["lang"]).strip().lower()
        params = {k: c[k] for k in ("yaw", "pitch") if k in c}
        if params:
            item["params"] = params
        out.append(item)
        if dur:
            back = None
            if action == "face":
                back = defaults.get("face") or "neutral"
            elif action == "lights":
                back = defaults.get("lights") or "normal"
            elif action == "dance":
                back = "stop"
            elif action == "head":
                back = "center"
            elif action == "vu" and value.lower() in VU_COLORS:
                back = (defaults.get("vu") or {}).get("color") or "classic"
            if back is not None:
                out.append({"at": at + dur, "action": action, "value": back, "seq": len(out), "restore": True})
    out.sort(key=lambda x: (x["at"], x["seq"]))
    for x in out:
        x.pop("seq", None)
    return out


def validate_setlist(doc):
    """-> (normalized set list, warnings). Raises SetlistError with a readable path."""
    if isinstance(doc, str):
        try:
            doc = json.loads(doc)
        except ValueError as e:
            raise SetlistError("not valid JSON: %s" % e)
    if not isinstance(doc, dict):
        raise SetlistError("a set list must be a JSON object")
    warnings = ["unknown key '%s' ignored" % k for k in doc if k not in _TOP_KEYS]
    if doc.get("format") != FORMAT:
        raise SetlistError("format must be \"%s\"" % FORMAT)
    title = str(doc.get("title") or "").strip()
    if not title:
        raise SetlistError("title is required")
    d = doc.get("defaults") or {}
    if not isinstance(d, dict):
        raise SetlistError("defaults must be an object")
    warnings += ["defaults: unknown key '%s' ignored" % k for k in d if k not in _DEFAULT_KEYS]
    defaults = {"groove": True, "gap": 2.0}
    if d.get("face") is not None:
        if not _face_ok(d["face"]):
            raise SetlistError("defaults.face '%s' is not one of her faces" % d["face"])
        defaults["face"] = str(d["face"]).strip().lower()
    if d.get("lights") is not None:
        if sh.map_cue("lights", str(d["lights"]))[0] is None:
            raise SetlistError("defaults.lights '%s' not supported" % d["lights"])
        defaults["lights"] = str(d["lights"]).strip().lower()
    if "groove" in d:
        if not isinstance(d["groove"], bool):
            raise SetlistError("defaults.groove must be true or false")
        defaults["groove"] = d["groove"]
    if "gap" in d:
        defaults["gap"] = _num(d["gap"], "defaults.gap", 0.0, 30.0)
    if "vu" in d:
        vu = d["vu"]
        if not isinstance(vu, dict):
            raise SetlistError("defaults.vu must be {on, color}")
        nv = {}
        if "on" in vu:
            if not isinstance(vu["on"], bool):
                raise SetlistError("defaults.vu.on must be true or false")
            nv["on"] = vu["on"]
        if vu.get("color") is not None:
            if str(vu["color"]).strip().lower() not in VU_COLORS:
                raise SetlistError("defaults.vu.color must be one of %s" % ", ".join(VU_COLORS))
            nv["color"] = str(vu["color"]).strip().lower()
        defaults["vu"] = nv
    for k in ("loop", "shuffle"):
        if k in doc and not isinstance(doc[k], bool):
            raise SetlistError("%s must be true or false" % k)
    songs = doc.get("songs")
    if not isinstance(songs, list) or not songs:
        raise SetlistError("songs must be a non-empty list")
    if len(songs) > MAX_SONGS:
        raise SetlistError("at most %d songs" % MAX_SONGS)
    seen, out = set(), []
    for i, s in enumerate(songs):
        w = "songs[%d]" % i
        if not isinstance(s, dict):
            raise SetlistError("%s must be an object" % w)
        warnings += ["%s: unknown key '%s' ignored" % (w, k) for k in s if k not in _SONG_KEYS]
        sid = s.get("id")
        if sid is None or (isinstance(sid, str) and not sid.strip()) or isinstance(sid, (bool, list, dict)):
            raise SetlistError("%s.id is required" % w)
        sid = str(sid).strip()
        if sid in seen:
            raise SetlistError("%s.id '%s' is used twice" % (w, sid))
        seen.add(sid)
        track = str(s.get("track") or "").strip()
        if not track:
            raise SetlistError("%s.track is required" % w)
        start = _num(s["start"], w + ".start", 0.0, 3600.0) if s.get("start") is not None else 0.0
        end = _num(s["end"], w + ".end", 0.0, 3600.0) if s.get("end") is not None else None
        if end is not None and end <= start:
            raise SetlistError("%s.end must be after start" % w)
        out.append({"id": sid, "track": track, "start": start, "end": end,
                    "intro": _lines(s.get("intro"), w + ".intro"),
                    "outro": _lines(s.get("outro"), w + ".outro"),
                    "cues": expand_cues(s.get("cues"), defaults, start, end, w + ".cues")})
    return ({"title": title, "defaults": defaults, "loop": bool(doc.get("loop", False)),
             "shuffle": bool(doc.get("shuffle", False)), "songs": out}, warnings)


# ── files and tracks ─────────────────────────────────────────────────────────
def setlists_dir():
    for d in SETLIST_DIRS:
        if os.path.isdir(d):
            return d
    return SETLIST_DIRS[0]


def list_setlists(d=None):
    d = d or setlists_dir()
    try:
        return sorted(f for f in os.listdir(d) if f.lower().endswith(".json") and not f.startswith(".")
                      and os.path.isfile(os.path.join(d, f)))
    except FileNotFoundError:
        return []


def load_setlist_file(path):
    with open(path, encoding="utf-8") as fh:
        return validate_setlist(fh.read())


def _stem_key(name):
    return re.sub(r"[^a-z0-9]+", " ", os.path.splitext(os.path.basename(str(name)))[0].lower()).strip()


def resolve_track(track, files):
    """Name -> music file, or None. Exact name (case, dashes and extension ignored) first,
    then play_music.py's own fuzzy match, then the Actor Mode music matcher."""
    if not files:
        return None
    key = _stem_key(track)
    for f in files:
        if _stem_key(f) == key:
            return f
    from plugins_func.functions import play_music as pm
    hit = pm._find_best_match(os.path.splitext(str(track))[0], files)
    return hit or sh.match_music(track, files)


# ── voice ────────────────────────────────────────────────────────────────────
_LEAD = r"^(?:(?:ok|okay|hey|um|uh|so|please|alright|nous girl|new girl|noose girl)\s+)*"
_DJ = r"(?:dj|d j|deejay|dee jay)"
_DJ_RE = re.compile(_LEAD + r"(?:(?:start|enter|turn on|switch to|go into|go to|activate|let's do|lets do|"
                    r"let's go|do|begin|play)\s+)?(?:the\s+|a\s+)?" + _DJ
                    + r"\s+(?:mode|time|set list|setlist|set)(?:\s+(?P<name>.*?))?(?:\s+please)?$")
_BE_MY_DJ = re.compile(_LEAD + r"be\s+(?:my|the|our)\s+" + _DJ + r"(?:\s+please)?$")
_SETLIST_RE = re.compile(_LEAD + r"(?:play|start)\s+(?:the\s+)?(?:set ?list)\s+(?P<name>.+?)(?:\s+please)?$")
_SET_NEXT = re.compile(_LEAD + r"(?:next|skip|skip it|skip this one|skip (?:this |the )?(?:song|track)|"
                       r"(?:play\s+)?(?:the\s+)?next\s+(?:song|track|one|tune))(?:\s+please)?$")
_SET_PREV = re.compile(_LEAD + r"(?:previous|back|go back|go back (?:a|one) (?:song|track)|back one (?:song|track)|"
                       r"(?:play\s+)?(?:the\s+)?(?:previous|last|prior)\s+(?:song|track|one|tune)(?:\s+again)?)"
                       r"(?:\s+please)?$")
_SET_STOP = re.compile(_LEAD + r"(?:stop|stop it|stop the music|stop music|stop the set|end the set|end set|"
                       r"that's enough|that is enough|enough|stop dj|stop the dj|stop playing|stop stop)(?:\s+please)?$")
_SET_RESUME = re.compile(_LEAD + r"(?:continue|resume|keep going|keep playing|play|go on|carry on|"
                         r"back to the music|play on)(?:\s+please)?$")
_DJ_OFF = re.compile(_LEAD + r"(?:(?:stop|end|exit|quit|leave|turn off|cancel)\s+(?:the\s+)?" + _DJ
                     + r"(?:\s+mode)?|" + _DJ + r"\s+mode\s+off)(?:\s+please)?$")
_JUST_PLAY = re.compile(r"^(?:just play|just music|no set ?list|random|anything|surprise me|just play music)$")


def _n(text):
    t = re.sub(r"[^\w\s']", " ", (text or "").lower().replace("’", "'"))
    return re.sub(r"\s+", " ", t).strip()


def parse_dj(text):
    """-> ("off",) | ("menu",) | ("start", name) | None"""
    t = _n(text)
    if not t:
        return None
    if _DJ_OFF.match(t):
        return ("off",)
    if _BE_MY_DJ.match(t):
        return ("menu",)
    m = _DJ_RE.match(t)
    if m:
        name = (m.group("name") or "").strip()
        if name in ("", "on", "now", "time"):
            return ("menu",)
        return ("start", name)
    m = _SETLIST_RE.match(t)
    if m:
        return ("start", m.group("name").strip())
    return None


def parse_set_command(text):
    """While a set plays or is paused: -> "next" | "prev" | "stop" | "resume" | None."""
    t = _n(text)
    if _SET_NEXT.match(t):
        return "next"
    if _SET_PREV.match(t):
        return "prev"
    if _SET_STOP.match(t):
        return "stop"
    if _SET_RESUME.match(t):
        return "resume"
    return None


def menu_sentence(menu):
    if len(menu) == 1:
        return ["I have one set list: number %s, %s." % (sh.num_word(menu[0][0]), menu[0][2]),
                "Say the number to start it, just play for any song, or never mind."]
    parts = "; ".join("%s, %s" % (sh.num_word(n), name) for n, _, name in menu)
    return ["I have %s set lists: %s." % (sh.num_word(len(menu)), parts), "Which one?"]


# ── seams (tests replace these) ───────────────────────────────────────────────
async def _tts_packets(conn, sentence, lang):
    return await sh._tts_packets(conn, sentence, lang)


async def _load_audio(path):
    from core.utils.util import audio_to_data
    return await audio_to_data(path, is_opus=True)


async def _device_call(conn, wire, args):
    await sh._device(conn, wire, args)


async def _stt(conn, text):
    from core.handle.sendAudioHandle import send_stt_message
    await send_stt_message(conn, text)


def _music_files(conn):
    from plugins_func.functions import play_music as pm
    return pm._hg_library(conn), pm.MUSIC_CACHE.get("music_dir", "./music")


# ── runtime ──────────────────────────────────────────────────────────────────
class SetState:
    def __init__(self, name, setlist):
        self.name = name
        self.setlist = setlist
        order = list(range(len(setlist["songs"])))
        if setlist.get("shuffle"):
            random.shuffle(order)
        self.order = order
        self.idx = 0
        self.task = None
        self.sid = None
        self.clock = 0.0
        self.paused = False
        self.paused_at = 0.0
        self.resume_pos = None      # frame to resume the current song from
        self.log = []               # (what, detail) for tests and the log
        self.done = False

    def song(self):
        return self.setlist["songs"][self.order[self.idx]]


def _log(conn):
    return conn.logger.bind(tag=TAG)


def _st(conn):
    st = getattr(conn, "hg_setlist", None)
    if st is not None and st.paused and time.time() - st.paused_at > PAUSED_FORGET_S:
        conn.hg_setlist = None
        return None
    return st


def _put(conn, st, kind, packets, text=None):
    ST = sh._sentence_type()
    conn.tts.tts_audio_queue.put((getattr(ST, kind), packets, text, st.sid))


def _alive(conn, st):
    return getattr(conn, "hg_setlist", None) is st and not st.done and conn.sentence_id == st.sid \
        and not getattr(conn, "client_abort", False)


async def _wait_until(st, t):
    d = t - time.time()
    if d > 0:
        await asyncio.sleep(d)


async def _speak(conn, st, lines):
    for ln in lines:
        for sent in sh.split_sentences(ln["text"]):
            lang = sh._detect_lang(sent, ln.get("lang"))
            try:
                packets = await _tts_packets(conn, sent, lang)
            except Exception as e:
                _log(conn).warning("set list TTS failed for '%s': %r" % (sent, e))
                continue
            if not _alive(conn, st):
                return False
            _put(conn, st, "FIRST", packets, sent)
            st.clock = max(time.time(), st.clock) + len(packets) * FRAME_S
            st.log.append(("say", sent))
    return True


def _cue_call(cue, defaults):
    """-> (wire, args) or (None, why)."""
    a, v = cue["action"], cue["value"]
    if a == "vu":
        return "self_dj_vu", {"mode": v.lower(), "seconds": int(round(cue.get("duration") or 2))}
    if a == "face" and v.lower() == "scowl":
        return "self_face_show", {"emotion": "scowl"}
    wire, args, why = sh.map_cue(a, v, cue.get("params"))
    return (wire, args) if wire else (None, why)


async def _fire(conn, st, cue):
    wire, args = _cue_call(cue, st.setlist["defaults"])
    st.log.append(("cue", "%s:%s@%g" % (cue["action"], cue["value"], cue["at"])))
    if wire is None:
        _log(conn).info("set list cue skipped: %s" % args)
        return
    asyncio.create_task(_device_call(conn, wire, args))


async def _apply_defaults(conn, st):
    d = st.setlist["defaults"]
    calls = []
    if d.get("face"):
        calls.append(_cue_call({"action": "face", "value": d["face"], "at": 0}, d))
    if d.get("lights"):
        calls.append(_cue_call({"action": "lights", "value": d["lights"], "at": 0}, d))
    vu = d.get("vu") or {}
    if vu.get("color"):
        calls.append(("self_dj_vu", {"mode": vu["color"], "seconds": 0}))
    if vu.get("on") is False:
        calls.append(("self_dj_vu", {"mode": "off", "seconds": 0}))
    elif vu.get("on") is True and not vu.get("color"):
        calls.append(("self_dj_vu", {"mode": "on", "seconds": 0}))
    for wire, args in calls:
        if wire:
            asyncio.create_task(_device_call(conn, wire, args))
    st.log.append(("defaults", len(calls)))


async def _play_song(conn, st, song, f, music_dir, from_pos=None):
    """Stream one song with its timed cues. True = finished, False = interrupted."""
    d = st.setlist["defaults"]
    title = os.path.splitext(os.path.basename(f))[0]
    asyncio.create_task(_device_call(conn, "self_dj_mode", {"action": "track", "track": title}))
    if from_pos is None and song["intro"]:
        if not await _speak(conn, st, song["intro"]):
            return False
    packets = await _load_audio(os.path.join(music_dir, f))
    if not _alive(conn, st):
        return False
    first = int(round(song["start"] / FRAME_S))
    last = len(packets) if song["end"] is None else min(len(packets), int(round(song["end"] / FRAME_S)))
    pos = first if from_pos is None else max(first, min(from_pos, last))
    if d.get("groove", True):
        sh._groove(conn, True)
    st.log.append(("song", "%s %s" % (song["id"], f)))
    _log(conn).info("set list '%s': %s (%s) frames %d..%d" % (st.name, song["id"], f, pos, last))
    # wall time at which song time `song_t` is heard: t0 + (song_t - pos_time)
    t0 = max(time.time(), st.clock)
    pos_time = pos * FRAME_S
    end_t = last * FRAME_S
    # cues already passed (resume) are dropped; cues past the real audio end land at the end
    cues = [dict(c, at=min(c["at"], end_t)) for c in song["cues"] if c["at"] >= pos_time - 1e-6]
    ci = 0
    st.resume_pos = pos
    while pos < last or ci < len(cues):
        if not _alive(conn, st):
            st.resume_pos = pos
            return False
        now = time.time()
        while ci < len(cues) and t0 + (cues[ci]["at"] - pos_time) <= now:
            c = cues[ci]
            ci += 1
            if c["action"] == "pause":
                st.log.append(("cue", "pause@%g" % c["at"]))
                await _wait_until(st, st.clock)
                await asyncio.sleep(c.get("duration") or 1.0)
                hold = time.time() - now
                t0 += hold
                st.clock = time.time()
            elif c["action"] == "voice":
                st.log.append(("cue", "voice@%g" % c["at"]))
                before = max(time.time(), st.clock)
                if not await _speak(conn, st, [{"text": c["value"], "lang": c.get("lang")}]):
                    st.resume_pos = pos
                    return False
                t0 += st.clock - before          # the song waits while she talks
            else:
                await _fire(conn, st, c)
            now = time.time()
        if pos >= last:
            if ci < len(cues):                   # cues past the audio: fire them at their time
                await asyncio.sleep(min(0.05, max(0.0, t0 + (cues[ci]["at"] - pos_time) - time.time())))
            continue
        if st.clock - now < LEAD_S:
            chunk = packets[pos:pos + CHUNK_FRAMES]
            _put(conn, st, "MIDDLE", chunk, None)
            pos += len(chunk)
            st.clock = max(now, st.clock) + len(chunk) * FRAME_S
            st.resume_pos = pos
        else:
            await asyncio.sleep(min(0.05, max(0.005, st.clock - now - LEAD_S)))
    await _wait_until(st, st.clock)
    if not _alive(conn, st):
        return False
    if song["outro"]:
        if not await _speak(conn, st, song["outro"]):
            return False
    st.resume_pos = None
    return True


async def _run(conn, st, heard, resume=False):
    try:
        conn.client_abort = False
        st.sid = uuid.uuid4().hex
        conn.sentence_id = st.sid
        st.paused = False
        await _stt(conn, heard)
        st.clock = time.time()
        if not resume:
            await _apply_defaults(conn, st)
        files, music_dir = _music_files(conn)
        missing = 0
        from_pos = st.resume_pos if resume else None
        while not st.done:
            song = st.song()
            f = resolve_track(song["track"], files)
            if f is None:
                missing += 1
                st.log.append(("missing", song["id"]))
                _log(conn).info("set list '%s': no track like '%s', skipping" % (st.name, song["track"]))
                if not await _speak(conn, st, [{"text": "I can't find %s. Skipping it." % song["track"].replace("-", " "), "lang": "en"}]):
                    return
                if missing >= len(st.setlist["songs"]):
                    await _speak(conn, st, [{"text": "None of the songs in this set are in my music folder.", "lang": "en"}])
                    break
            else:
                missing = 0
                ok = await _play_song(conn, st, song, f, music_dir, from_pos)
                from_pos = None
                if not ok:
                    if getattr(conn, "hg_setlist", None) is st and not st.done:
                        st.paused, st.paused_at = True, time.time()
                        sh._groove(conn, False)
                        _log(conn).info("set list '%s' paused at %s" % (st.name, song["id"]))
                    return
            if st.idx + 1 >= len(st.order):
                if not st.setlist["loop"]:
                    break
                st.idx = 0
            else:
                st.idx += 1
            gap = st.setlist["defaults"].get("gap", 2.0)
            if gap:
                await _wait_until(st, st.clock)
                await asyncio.sleep(gap)
                st.clock = time.time()
            if not _alive(conn, st):
                if getattr(conn, "hg_setlist", None) is st and not st.done:
                    st.paused, st.paused_at, st.resume_pos = True, time.time(), None
                return
        await _wait_until(st, st.clock)
        _finish(conn, st, "set over")
    except asyncio.CancelledError:
        raise
    except Exception as e:
        import traceback
        _log(conn).error("set list error: %r %s" % (e, traceback.format_exc()))
        _finish(conn, st, "error")


def _finish(conn, st, why):
    st.done = True
    try:
        _put(conn, st, "LAST", [], None)
    except Exception:
        pass
    sh._groove(conn, False)
    if getattr(conn, "hg_setlist", None) is st:
        conn.hg_setlist = None
    st.log.append(("end", why))
    _log(conn).info("set list '%s' ended: %s" % (st.name, why))


def _cancel(st):
    if st.task and not st.task.done() and st.task is not asyncio.current_task():
        st.task.cancel()


async def start_setlist(conn, f, heard, d=None):
    d = d or setlists_dir()
    try:
        setlist, warnings = load_setlist_file(os.path.join(d, f))
    except (SetlistError, OSError) as e:
        _log(conn).warning("set list %s unusable: %s" % (f, e))
        await sh._say(conn, ["That set list has a mistake in it."], heard, "en")
        return True
    for w in warnings:
        _log(conn).info("set list %s: %s" % (f, w))
    old = getattr(conn, "hg_setlist", None)
    if old is not None:
        old.done = True
        _cancel(old)
    from plugins_func.functions import play_music as pm
    pm.hg_set_dj(conn, True)
    st = SetState(f, setlist)
    conn.hg_setlist = st
    conn.hg_dj_pick = None
    _log(conn).info("set list '%s' started (%d songs) by '%s'" % (f, len(setlist["songs"]), heard))
    st.task = asyncio.create_task(_run(conn, st, heard))
    return True


async def _step(conn, st, cmd, heard):
    st.done = False
    _cancel(st)
    n = len(st.order)
    if cmd == "next":
        if st.idx + 1 >= n:
            if not st.setlist["loop"]:
                _finish(conn, st, "next past the last song")
                await sh._say(conn, ["That's the end of my set."], heard, "en")
                return True
            st.idx = 0
        else:
            st.idx += 1
        st.resume_pos = None
    elif cmd == "prev":
        if st.idx == 0:
            st.idx = n - 1 if st.setlist["loop"] else 0
        else:
            st.idx -= 1
        st.resume_pos = None
    st.log.append(("step", cmd))
    st.task = asyncio.create_task(_run(conn, st, heard, resume=(cmd == "resume")))
    return True


async def hg_try_setlist(conn, text):
    """True if this utterance belongs to DJ set lists (so no LLM)."""
    try:
        return await _hg_try_setlist(conn, text)
    except Exception as e:
        import traceback
        _log(conn).error("set list error: %r %s" % (e, traceback.format_exc()))
        return False


async def _hg_try_setlist(conn, text):
    st = _st(conn)
    if st is not None:
        if parse_dj(text) == ("off",):
            st.done = True
            _cancel(st)
            _finish(conn, st, "DJ mode off")
            from plugins_func.functions import play_music as pm
            pm.hg_set_dj(conn, False)
            await sh._say(conn, ["DJ mode off."], text, "en")
            return True
        cmd = parse_set_command(text)
        if cmd == "stop":
            st.done = True
            _cancel(st)
            _finish(conn, st, "stop")
            await sh._say(conn, ["Set over."], text, "en")
            return True
        if cmd in ("next", "prev"):
            return await _step(conn, st, cmd, text)
        if cmd == "resume":
            if st.paused:
                return await _step(conn, st, "resume", text)
            return True                          # already playing
    trig = parse_dj(text)
    d = setlists_dir()
    files = list_setlists(d)
    pick = getattr(conn, "hg_dj_pick", None)
    if trig is None and pick is not None:
        return await _handle_pick(conn, pick, text, d, files)
    if trig is None or trig == ("off",) or not files:
        return False                             # play_music.py handles it (no set lists: plain DJ mode)
    if trig[0] == "start":
        f = sh.resolve_choice(trig[1], files)
        if f:
            return await start_setlist(conn, f, text, d)
        return await _offer(conn, files, text, prefix="I don't have a set list called %s." % trig[1])
    return await _offer(conn, files, text)


async def _offer(conn, files, text, prefix=""):
    menu = sh.number_scenes(files)
    from plugins_func.functions import play_music as pm
    pm.hg_set_dj(conn, True)
    end = await sh._say(conn, ([prefix] if prefix else []) + menu_sentence(menu), text, "en")
    conn.hg_dj_pick = {"deadline": max(time.time(), end or 0) + PICK_TIMEOUT_S, "misses": 0}
    _log(conn).info("DJ set list menu offered (%d)" % len(menu))
    return True


async def _handle_pick(conn, pick, text, d, files):
    if time.time() > pick["deadline"]:
        conn.hg_dj_pick = None
        return False
    if sh.is_cancel(text):
        conn.hg_dj_pick = None
        await sh._say(conn, ["Okay, maybe later."], text, "en")
        return True
    if _JUST_PLAY.match(_n(text)):
        conn.hg_dj_pick = None
        from plugins_func.functions import play_music as pm
        return await pm.hg_try_dj_shortcut(conn, "next song", setlists=False)   # plain DJ mode
    f = sh.resolve_choice(text, files)
    if f:
        return await start_setlist(conn, f, text, d)
    pick["misses"] += 1
    if pick["misses"] >= 2:
        conn.hg_dj_pick = None
        return False
    end = await sh._say(conn, ["Hmm, I don't have that one. Say a number, or never mind."], text, "en")
    pick["deadline"] = max(time.time(), end or 0) + PICK_TIMEOUT_S
    return True
