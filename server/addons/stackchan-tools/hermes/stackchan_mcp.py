#!/usr/bin/env python3
"""StackChan (Nous-Girl) robot tools for Hermes Agent, as a stdio MCP server. ADD-ONLY build.

v3.6.3 follow tool (staged 2026-10-23): adds robot_follow_faces -> device self.camera.follow_faces.
v3.6.7-radio (staged 2026-10-27): adds robot_radio -> bridge POST /hg/api/radio (no device tool).
meme-screen v1 (staged 2026-10-28): adds robot_show_meme -> bridge POST /hg/api/show_meme. Reads a saved
  meme from ~/NousGirl-memes (NG_MEMES_DIR), prefers <stem>-320x240.png as is; only if there is no PNG it
  converts <stem>-320x240.jpg (or the original) to a 320x240 PNG in memory (Pillow, compress_level=1).
  Nothing is written to disk. No device tool is added to DEVICE_TOOLS.
small-preview (staged 2026-10-28): robot_show_meme now always sends a 160x120 opaque PNG (RGB, no alpha,
  letterboxed on black), made in memory from the saved 320x240 PNG (else the JPEG or original). The
  320x240 preview ran firmware v3.6.7-dj6 out of memory. Same size on screen (the firmware scales a
  160-wide preview to 1.0). Saved files in ~/NousGirl-memes are not changed.

ADD-ONLY (2026-10-23, the owner's choice): this server is ADDED to the nous-girl profile next to her
existing toolsets. Nothing is locked down or removed; platform_toolsets stays exactly as it is.
Code is stackchan_mcp_v3.py + the face ID patch (hermes/faceid/), i.e. 8 robot tools plus the
read-only robot_who_is_here ({"name": "<FirstName>"} or {"name": "unknown"}).
robot_what_do_you_see strips the internal "__hg_" marker so it cannot reach face ID.
The caps below still apply to THESE tools. Note: because her other toolsets (terminal, file, ...)
stay enabled, the caps are a guard rail for normal use, not a hard security boundary.

Install: see server/addons/stackchan-tools/README.md.

v2 changes (Sol review 2026-10-23, B2/B3):
  * Allowlist only. The voice path gets 8 tools: lights, face, look, camera
    (describe), dance, server music, SD music, status. No SD notes/list,
    no photo saving, no person profiles, no face embeddings, no dance recording.
    There is NO generic passthrough: every tool hard-codes its device tool name,
    and _device() refuses any name outside DEVICE_TOOLS.
  * Server-side caps, enforced here before anything reaches the bridge:
      head  yaw -90..90, pitch 3..85 (3 = level, facing forward, also her lowest; higher = up), speed 100..600 (firmware hard limits are
            yaw -128..128, pitch 3..87, speed 0..1000; see cores3_servo.h/.cc)
      lights brightness 0..80; action/mood/cycle/speed/color from fixed lists
      dance  actions preset|play|stop|list only; presets wiggle|nod|disco|robot|sway;
             saved dances only if named in STACKCHAN_SAVED_DANCES; speed 50..150,
             loops 1..5; unknown actions are rejected (no fall-through to play)
      camera question <=300 chars, at most one photo per 5 s (loop guard)
    Numbers outside a range are clamped and the reply says "clamped": true.
    Unknown strings are rejected with ok=false.

Env:
  HG_BRIDGE_URL          default http://127.0.0.1:8003
  HG_BRIDGE_TOKEN        must equal hg_bridge.token in data/.config.yaml (never logged)
  STACKCHAN_SAVED_DANCES optional comma list of recorded dance names she may play
Requires: "mcp>=1.2,<2" httpx
"""
import json
import os
import re
import time

import httpx
from mcp.server.fastmcp import FastMCP

BASE = os.environ.get("HG_BRIDGE_URL", "http://127.0.0.1:8003").rstrip("/")
TOKEN = os.environ.get("HG_BRIDGE_TOKEN", "")

# ---- limits (single source of truth, unit-tested) -------------------------
FW_YAW = (-128, 128)          # firmware cores3_servo.h kYawMin/kYawMax
FW_PITCH = (3, 87)            # firmware kPitchMin/kPitchMax
YAW = (-90, 90)               # voice cap, inside firmware range
PITCH = (3, 85)              # 3 = level (firmware kLevelPitchDeg 3, checked on the robot); 85 = ceiling
PITCH_LEVEL = 3
HEAD_SPEED = (100, 600)
HEAD_SPEED_DEFAULT = 400
BRIGHTNESS = (0, 80)
BRIGHTNESS_DEFAULT = 60
DANCE_SPEED = (50, 150)       # firmware allows 50..200
DANCE_LOOPS = (1, 5)          # firmware allows 1..10
PHOTO_MIN_INTERVAL_S = 5.0
QUESTION_MAX = 300

LIGHT_ACTIONS = {"on", "off", "color", "mood", "cycle", "normal"}
LIGHT_MOODS = {"happy", "calm", "party", "spooky", "sleepy", "focus"}
LIGHT_CYCLES = {"rainbow", "breathe"}
LIGHT_SPEEDS = {"slow", "fast"}
COLOR_NAMES = {"red", "orange", "yellow", "green", "lime", "teal", "cyan", "blue", "navy",
               "purple", "violet", "magenta", "pink", "white", "warm white", "gold",
               "brown", "gray"}
HEX_COLOR = re.compile(r"^#[0-9A-Fa-f]{6}$")
EMOTIONS = {"neutral", "happy", "sad", "thinking", "sleeping", "pet", "shake",
            "speaking", "listening", "look_left", "look_right",
            "scowl"}   # v3.6.7-dj firmware: a fierce angry look
DANCE_ACTIONS = {"preset", "play", "stop", "list"}
DANCE_PRESETS = {"wiggle", "nod", "disco", "robot", "sway"}
SD_MUSIC_ACTIONS = {"play", "stop", "list", "now"}
SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _.\-]{0,79}$")
DANCE_NAME = re.compile(r"^[A-Za-z0-9_\-]{1,32}$")

# The ONLY device tools this server can ever call.
DEVICE_TOOLS = frozenset({
    "self.led.light", "self.face.show", "self.servo.set_angles",
    "self.camera.take_photo", "self.dance.preset", "self.dance.play",
    "self.dance.stop", "self.dance.list", "self.music.sd",
    "self.camera.follow_faces",   # v3.6.3 firmware: face following on/off
    "self.dj.mode",               # v3.6.7-dj firmware: DJ screen on/off
})
# Explicitly NOT exposed to the voice path (documented in the proposal, asserted in tests).
EXCLUDED_DEVICE_TOOLS = frozenset({
    "self.sd.list", "self.sd.read_text", "self.sd.write_text", "self.sd.save_photo",
    "self.sd.status", "self.profile.save", "self.profile.load", "self.profile.list",
    "self.face_cache.save", "self.face_cache.load", "self.face_cache.list",
    "self.dance.record_start", "self.dance.record_stop", "self.dance.groove",
    "self.led.set_color", "self.led.set_default", "self.led.set_mode", "self.led.off",
    "self.led.auto", "self.servo.set_default", "self.servo.look_around",
    "self.server.set_ota_url", "self.settings.open", "self.audio_speaker.set_volume",
    "self.motion.set_auto",
})
VOICE_TOOLS = ("robot_lights", "robot_face", "robot_look", "robot_what_do_you_see",
               "robot_dance", "robot_play_music", "robot_music_sd", "robot_status",
               "robot_who_is_here", "robot_follow_faces",
               "robot_music_skip", "robot_dj_mode", "robot_rave_mode",   # v3.6.7-dj
               "robot_radio",                         # v3.6.7-radio
               "robot_show_meme")                     # meme-screen v1

# ---- face ID (read-only) ----
FACEID_QUESTION = "__hg_faceid__ identify"
FACEID_REPLY = re.compile(r"HGFACEID:v1:name=([A-Za-z]{1,20});")
FACEID_NAME = re.compile(r"^[A-Z][a-z]{1,19}$")
WHO_CACHE_S = 60.0
_who_cache = [0.0, "unknown"]

_last_photo = [0.0]
mcp = FastMCP("stackchan")


class Rejected(ValueError):
    pass


def _err(msg: str) -> str:
    return json.dumps({"ok": False, "error": msg})


def _as_int(value, name: str) -> int:
    if isinstance(value, bool):
        raise Rejected(f"{name} must be a number")
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value == value and abs(value) < 1e6:
        return int(round(value))
    if isinstance(value, str) and re.fullmatch(r"\s*-?\d{1,6}\s*", value):
        return int(value)
    raise Rejected(f"{name} must be a number")


def _clamp(value, lo_hi, name: str, notes: list) -> int:
    v = _as_int(value, name)
    lo, hi = lo_hi
    c = min(max(v, lo), hi)
    if c != v:
        notes.append(f"{name} {v}->{c}")
    return c


def _pick(value, allowed, name: str) -> str:
    v = (value if isinstance(value, str) else "").strip().lower()
    if v not in allowed:
        raise Rejected(f"{name} must be one of: {', '.join(sorted(allowed))}")
    return v


def _saved_dances() -> set:
    raw = os.environ.get("STACKCHAN_SAVED_DANCES", "")
    return {n.strip() for n in raw.split(",") if DANCE_NAME.fullmatch(n.strip() or "-x-") and n.strip()}


# ---- transport (mocked in tests) -------------------------------------------
def _transport_post(path: str, payload: dict, timeout: float = 60.0) -> str:
    try:
        r = httpx.post(BASE + path, json=payload, timeout=timeout,
                       headers={"Authorization": "Bearer " + TOKEN})
        return r.text[:4000]
    except httpx.HTTPError as e:
        return _err(f"bridge unreachable ({type(e).__name__})")


def _transport_get(path: str, timeout: float = 10.0) -> str:
    try:
        r = httpx.get(BASE + path, timeout=timeout,
                      headers={"Authorization": "Bearer " + TOKEN})
        return r.text[:4000]
    except httpx.HTTPError as e:
        return _err(f"bridge unreachable ({type(e).__name__})")


def _device(tool: str, args: dict, notes=None) -> str:
    if tool not in DEVICE_TOOLS:
        return _err("tool not allowed on the voice path")
    out = _transport_post("/hg/api/device_call", {"tool": tool, "args": args})
    if notes:
        try:
            d = json.loads(out)
            if isinstance(d, dict):
                d["clamped"] = True
                d["clamp_notes"] = notes
                return json.dumps(d)
        except ValueError:
            pass
        return json.dumps({"result": out, "clamped": True, "clamp_notes": notes})
    return out


# ---- pure validators (unit-tested) -----------------------------------------
def build_lights(action="color", color="", mood="", cycle="rainbow", speed="slow",
                 brightness=BRIGHTNESS_DEFAULT):
    notes = []
    a = _pick(action, LIGHT_ACTIONS, "action")
    args = {"action": a, "color": "", "mood": "", "cycle": "rainbow", "speed": "slow",
            "brightness": _clamp(brightness, BRIGHTNESS, "brightness", notes)}
    col = (color or "").strip() if isinstance(color, str) else ""
    if col:
        low = col.lower().replace("_", " ")
        if low in COLOR_NAMES:
            col = low
        elif not HEX_COLOR.fullmatch(col):
            raise Rejected("color must be a basic color name or #RRGGBB")
    if a == "color" and not col:
        raise Rejected("color is required for action=color")
    if a in ("on", "color", "cycle"):
        args["color"] = col
    if a == "mood":
        args["mood"] = _pick(mood, LIGHT_MOODS, "mood")
    if a == "cycle":
        args["cycle"] = _pick(cycle or "rainbow", LIGHT_CYCLES, "cycle")
    if a in ("cycle", "mood"):
        args["speed"] = _pick(speed or "slow", LIGHT_SPEEDS, "speed")
    return args, notes


def build_look(yaw=0, pitch=PITCH_LEVEL, speed=HEAD_SPEED_DEFAULT):
    notes = []
    args = {"yaw": _clamp(yaw, YAW, "yaw", notes),
            "pitch": _clamp(pitch, PITCH, "pitch", notes),
            "speed": _clamp(speed, HEAD_SPEED, "speed", notes)}
    return args, notes


def build_dance(action="preset", name="disco", speed=100, loops=2):
    """Returns (device_tool, args, notes)."""
    notes = []
    a = _pick(action, DANCE_ACTIONS, "action")
    if a == "stop":
        return "self.dance.stop", {}, notes
    if a == "list":
        return "self.dance.list", {}, notes
    n = (name or "").strip().lower() if isinstance(name, str) else ""
    lp = _clamp(loops, DANCE_LOOPS, "loops", notes)
    if a == "preset":
        if n not in DANCE_PRESETS:
            raise Rejected("preset must be one of: " + ", ".join(sorted(DANCE_PRESETS)))
        return "self.dance.preset", {"name": n, "loops": lp}, notes
    # a == "play": recorded dance, only from the configured allowlist
    raw = name.strip() if isinstance(name, str) else ""
    if raw not in _saved_dances():
        raise Rejected("that saved dance is not on her allowed list; use a preset: "
                       + ", ".join(sorted(DANCE_PRESETS)))
    sp = _clamp(speed, DANCE_SPEED, "speed", notes)
    return "self.dance.play", {"name": raw, "speed": sp, "loops": lp}, notes


def clean_text(s, limit: int) -> str:
    s = s if isinstance(s, str) else ""
    s = re.sub(r"[\x00-\x1f\x7f]", " ", s).strip()
    return s[:limit]


def clean_song(s) -> str:
    s = clean_text(s, 80)
    if s and not SAFE_NAME.fullmatch(s):
        raise Rejected("song name may only use letters, numbers, spaces, - _ .")
    if ".." in s:
        raise Rejected("bad song name")
    return s


# ---- MCP tools (the voice allowlist) ----------------------------------------
def _run(fn):
    try:
        return fn()
    except Rejected as e:
        return _err(str(e))


@mcp.tool()
def robot_lights(action: str = "color", color: str = "", mood: str = "",
                 cycle: str = "rainbow", speed: str = "slow",
                 brightness: int = BRIGHTNESS_DEFAULT) -> str:
    """Her 12 RGB lights. action: on|off|color|mood|cycle|normal. color: red, orange, yellow,
    green, lime, teal, cyan, blue, navy, purple, violet, magenta, pink, white, warm white, gold,
    brown, gray, or #RRGGBB. mood: happy|calm|party|spooky|sleepy|focus. cycle: rainbow|breathe.
    speed: slow|fast. brightness 0-80 (capped at 80). Use action=normal to go back to normal."""
    def go():
        args, notes = build_lights(action, color, mood, cycle, speed, brightness)
        return _device("self.led.light", args, notes)
    return _run(go)


@mcp.tool()
def robot_face(emotion: str = "neutral") -> str:
    """Show a face: neutral, happy, sad, thinking, sleeping, pet, shake, speaking, listening,
    look_left, look_right, scowl (a fierce angry look, for playful attitude)."""
    return _run(lambda: _device("self.face.show", {"emotion": _pick(emotion, EMOTIONS, "emotion")}))


@mcp.tool()
def robot_look(yaw: int = 0, pitch: int = 3, speed: int = HEAD_SPEED_DEFAULT) -> str:
    """Turn her head. yaw -90..90 (negative = her left). pitch 3 = level, facing forward.
    3 is also her lowest: she cannot look below level, so for 'look down', 'look straight' or
    'normal' use 3. HIGHER numbers look UP: about 20 = up a bit, about 40 to 60 = look up,
    85 = the ceiling. Pitch range 3..85, speed 100..600. Out-of-range values are clamped.
    If face following is on, her head is moving by itself: only use this when asked for a
    specific head move (or turn following off first with robot_follow_faces(enabled=false))."""
    def go():
        args, notes = build_look(yaw, pitch, speed)
        return _device("self.servo.set_angles", args, notes)
    return _run(go)


@mcp.tool()
def robot_what_do_you_see(question: str = "What do you see? Name the objects.") -> str:
    """Take ONE photo with her camera and get a description. Only when the person asks her
    to look. At most one photo every 5 seconds. Photos are not saved."""
    def go():
        now = time.monotonic()
        if now - _last_photo[0] < PHOTO_MIN_INTERVAL_S:
            raise Rejected("camera cooling down; answer from the last photo")
        q = re.sub(r"__hg_\w*", "", clean_text(question, QUESTION_MAX)).strip() \
            or "What do you see? Name the objects."
        _last_photo[0] = now
        return _device("self.camera.take_photo", {"question": q})
    return _run(go)


@mcp.tool()
def robot_dance(action: str = "preset", name: str = "disco", speed: int = 100,
                loops: int = 2) -> str:
    """Dance. action: preset | play | stop | list. preset name: wiggle | nod | disco | robot |
    sway. play = a dance you recorded and allowed by name. loops 1-5, speed 50-150 (play only)."""
    def go():
        tool, args, notes = build_dance(action, name, speed, loops)
        return _device(tool, args, notes)
    return _run(go)


@mcp.tool()
def robot_play_music(song: str = "", dance: str = "", dj: bool = False) -> str:
    """Play a song from the server music folder through her speaker (empty = random).
    Optional dance: a preset (wiggle | nod | disco | robot | sway) started with the song.
    dj=true: also turn DJ mode on (DJ screen with turntables and VU meters, groove to the beat)."""
    def go():
        s = clean_song(song)
        d = ""
        if dance:
            d = _pick(dance, DANCE_PRESETS, "dance")
        payload = {"song": s}
        if build_flag(dj, "dj"):
            payload["dj"] = True
        raw = _transport_post("/hg/api/play_music", payload)
        try:
            music = json.loads(raw or "{}")
        except ValueError:
            music = {"raw": raw[:500]}
        out = {"music": music}
        if d:
            out["dance"] = _device("self.dance.preset", {"name": d, "loops": 3})
        return json.dumps(out)
    return _run(go)


@mcp.tool()
def robot_music_sd(action: str = "play", track: str = "") -> str:
    """Music on her own microSD card. action: play | stop | list | now.
    track: part of a file name, or empty for random."""
    def go():
        a = _pick(action, SD_MUSIC_ACTIONS, "action")
        return _device("self.music.sd", {"action": a, "track": clean_song(track) if a == "play" else ""})
    return _run(go)


SKIP_ALIASES = {"next": "next", "forward": "next", "skip": "next",
                "prev": "prev", "previous": "prev", "back": "prev", "last": "prev"}


def build_flag(v, name: str) -> bool:
    """A real yes/no (bool, 0/1, or the usual words). Anything else is rejected."""
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)) and v in (0, 1):
        return bool(v)
    if isinstance(v, str):
        t = v.strip().lower()
        if t in ("true", "on", "yes", "1", "start"):
            return True
        if t in ("false", "off", "no", "0", "stop", ""):
            return False
    raise Rejected(f"{name} must be true or false")


def build_skip(direction) -> str:
    return SKIP_ALIASES[_pick(direction, set(SKIP_ALIASES), "direction")]


@mcp.tool()
def robot_music_skip(direction: str = "next") -> str:
    """Skip to the next song, or go back to the previous one, from the server music folder.
    direction: next | previous. Uses her real play history, so "previous song" / "play the
    last song again" / "go back a song" returns to what actually played before."""
    def go():
        d = build_skip(direction)
        raw = _transport_post("/hg/api/music/skip", {"direction": d})
        try:
            return json.dumps({"music": json.loads(raw or "{}")})
        except ValueError:
            return json.dumps({"music": {"raw": raw[:500]}})
    return _run(go)


@mcp.tool()
def robot_dj_mode(on: bool = True, start_song: bool = True) -> str:
    """DJ mode: while a song plays she shows a DJ screen (turntables, glowing VU meters
    bouncing to the music) and grooves to the beat. on=true for "DJ mode", "be my DJ";
    on=false for "DJ mode off", "stop DJ". start_song=true also starts a song when turning on."""
    def go():
        want = build_flag(on, "on")
        start = build_flag(start_song, "start_song")
        out = {"dj": _device("self.dj.mode", {"action": "on" if want else "off"})}
        if want and start:
            raw = _transport_post("/hg/api/play_music", {"song": "", "dj": True})
            try:
                out["music"] = json.loads(raw or "{}")
            except ValueError:
                out["music"] = {"raw": raw[:500]}
        return json.dumps(out)
    return _run(go)


# v3.6.7-rave BEGIN: robot_rave_mode (rave add-on)
RAVE_NAME = re.compile(r"[\w' &.,()-]{0,80}")


def build_rave_name(v, name: str) -> str:
    s = re.sub(r"\s+", " ", v if isinstance(v, str) else "").strip()
    if not RAVE_NAME.fullmatch(s):
        raise Rejected(f"{name} must be a short name (letters, numbers, spaces)")
    return s


@mcp.tool()
def robot_rave_mode(on: bool = True, start_song: bool = True, song: str = "", station: str = "") -> str:
    """Rave mode (needs v3.6.7-dj4 firmware or later): on the DJ screen the whole background
    changes color with the music (soft, at most 3 pulses a second, never a red flash), with the
    EQ, the lights and her dancing on the beat. on=true for "rave mode", "start a rave",
    "let's rave" (also turns DJ mode on); on=false for "rave mode off" (back to the normal DJ
    screen, the music keeps playing). start_song=true also starts music when turning on:
    station = an internet radio station (only if the radio is installed), or song = a song name
    from the server music folder, or both empty = a random song. For "stop the rave": on=false, then
    stop the music. Keep your reply to a few words; never talk over the music."""
    def go():
        want = build_flag(on, "on")
        start = build_flag(start_song, "start_song")
        sg = build_rave_name(song, "song")
        stn = build_rave_name(station, "station")
        out = {"rave": _device("self.dj.mode", {"action": "rave_on" if want else "rave_off"})}
        if want and start:
            if stn:
                path, payload = "/hg/api/radio", {"action": "play", "station": stn}
            else:
                path, payload = "/hg/api/play_music", {"song": sg, "dj": True}
            raw = _transport_post(path, payload, timeout=20.0)
            try:
                out["music"] = json.loads(raw or "{}")
            except ValueError:
                out["music"] = {"raw": raw[:500]}
        return json.dumps(out)
    return _run(go)
# v3.6.7-rave END


# ---- v3.6.7-radio: internet radio (stations live in the server's data/radio/stations.json) ----
RADIO_ACTIONS = {"play": "play", "on": "play", "start": "play", "switch": "play", "tune": "play",
                 "next": "next", "skip": "next", "forward": "next",
                 "prev": "prev", "previous": "prev", "back": "prev",
                 "stop": "stop", "off": "stop",
                 "list": "list", "stations": "list",
                 "status": "status", "what": "status",
                 "resume": "resume", "continue": "resume"}
RADIO_STATION = re.compile(r"^[\w .,'&:+-]{0,60}$")


def build_radio(action="play", station="") -> dict:
    a = RADIO_ACTIONS[_pick(action, set(RADIO_ACTIONS), "action")]
    st = station if isinstance(station, str) else ""
    st = re.sub(r"\s+", " ", st).strip()
    if not RADIO_STATION.fullmatch(st):
        raise Rejected("station must be a short station name (letters, numbers, spaces)")
    out = {"action": a}
    if a == "play" and st:
        out["station"] = st
    return out


@mcp.tool()
def robot_radio(action: str = "play", station: str = "") -> str:
    """Internet radio through her speaker (streams from the server; the DJ screen EQ and lights
    follow it). action: play | next | previous | stop | list | status | resume.
    station: a station name or nickname for action=play (empty = the last station played),
    e.g. "Groove Salad", "Drone Zone", "Secret Agent", "Radio Paradise", "jazz", "classical",
    "AgentRadio". Use list to hear every station. next/previous step through the stations.
    She announces the station herself when it starts, so keep your own reply to a few words."""
    def go():
        payload = build_radio(action, station)
        raw = _transport_post("/hg/api/radio", payload, timeout=20.0)
        try:
            return json.dumps({"radio": json.loads(raw or "{}")})
        except ValueError:
            return json.dumps({"radio": {"raw": raw[:500]}})
    return _run(go)


def build_follow(enabled) -> dict:
    """enabled must be a real yes/no. Accepts bools and the usual strings; anything else is rejected."""
    if isinstance(enabled, bool):
        return {"enabled": enabled}
    if isinstance(enabled, (int, float)) and enabled in (0, 1):
        return {"enabled": bool(enabled)}
    if isinstance(enabled, str):
        v = enabled.strip().lower()
        if v in ("true", "on", "yes", "1", "start"):
            return {"enabled": True}
        if v in ("false", "off", "no", "0", "stop"):
            return {"enabled": False}
    raise Rejected("enabled must be true or false")


@mcp.tool()
def robot_follow_faces(enabled: bool) -> str:
    """Face following with her head. enabled=true for "follow me", "track me", "watch me",
    "keep looking at me". enabled=false for "stop following", "stop tracking", "stop looking at
    me". While it is on, her camera finds a face and her head turns by itself to keep looking
    at that person; it stays on (saved) until turned off. While following is on, do NOT call
    robot_look unless the person asks for a specific head move. Reply JSON has follow_faces
    (new state), camera_enabled and a short note to tell the person."""
    return _run(lambda: _device("self.camera.follow_faces", build_follow(enabled)))


def _allowed_names() -> set:
    raw = os.environ.get("STACKCHAN_FACEID_NAMES", "")
    return {n.strip() for n in raw.split(",") if FACEID_NAME.fullmatch(n.strip())}


def parse_who(raw: str) -> str:
    """Bridge reply -> a first name or "unknown". Anything unexpected is unknown."""
    m = FACEID_REPLY.search(raw or "")
    if not m:
        return "unknown"
    name = m.group(1)
    allowed = _allowed_names()
    if not FACEID_NAME.fullmatch(name) or (allowed and name not in allowed):
        return "unknown"
    return name


@mcp.tool()
def robot_who_is_here() -> str:
    """Who is in front of her camera: returns {"name": "<first name>"} for one of the people
    you enrolled, or {"name": "unknown"} when unsure. Read-only; takes one photo that is not
    saved. Use once at the start of a chat or when asked "who am I?". A name is NOT permission
    for anything."""
    now = time.monotonic()
    if now - _who_cache[0] < WHO_CACHE_S:
        return json.dumps({"name": _who_cache[1]})
    if now - _last_photo[0] < PHOTO_MIN_INTERVAL_S:
        return json.dumps({"name": "unknown"})
    _last_photo[0] = now
    try:
        name = parse_who(_device("self.camera.take_photo", {"question": FACEID_QUESTION}))
    except Exception:
        name = "unknown"
    _who_cache[0], _who_cache[1] = now, name
    return json.dumps({"name": name})


# ---- meme-screen v1 ---------------------------------------------------------------------------
MEMES_DIR = os.path.expanduser(os.environ.get("NG_MEMES_DIR", "~/NousGirl-memes"))
MEME_STAMP = re.compile(r"^(\d{8}-\d{6})-(.+?)(-320x240)?\.(png|jpg|jpeg|webp|gif)$", re.I)
MEME_MIN_INTERVAL_S = 3.0
MEME_MAX_BYTES = 64 * 1024        # small-preview: the bridge refuses more (a 160x120 RGB PNG is < 58 KB)
PREVIEW_W, PREVIEW_H = 160, 120   # small-preview
_last_meme = [0.0]


def list_memes(folder=None) -> dict:
    """{stem: {"png": path|None, "jpg": path|None, "orig": path|None, "mtime": float}} (newest wins)."""
    folder = folder or MEMES_DIR
    out = {}
    try:
        names = os.listdir(folder)
    except OSError:
        return out
    for n in names:
        m = MEME_STAMP.match(n)
        p = os.path.join(folder, n)
        if not m or not os.path.isfile(p) or os.path.islink(p):
            continue
        stem = m.group(1) + "-" + m.group(2)
        e = out.setdefault(stem, {"png": None, "jpg": None, "orig": None, "mtime": 0.0})
        ext = m.group(4).lower()
        if m.group(3) and ext == "png":
            e["png"] = p
        elif m.group(3) and ext in ("jpg", "jpeg"):
            e["jpg"] = p
        elif not m.group(3):
            e["orig"] = p
        e["mtime"] = max(e["mtime"], os.path.getmtime(p))
    return out


def pick_meme(name="latest", folder=None):
    """Stem for "latest"/"last"/"" or the newest meme whose name contains all the words. None if none."""
    memes = list_memes(folder)
    if not memes:
        return None
    order = sorted(memes, key=lambda s: (memes[s]["mtime"], s), reverse=True)
    words = re.findall(r"[a-z0-9]+", (name if isinstance(name, str) else "").lower())[:6]
    if not words or words in (["latest"], ["last"], ["newest"], ["recent"], ["it"], ["that"]):
        return order[0]
    for s in order:
        if all(w in s.lower() for w in words):
            return s
    return None


def meme_png_bytes(entry) -> bytes:
    """small-preview: a 160x120 opaque PNG made in memory from the saved 320x240 PNG (else the JPEG,
    else the original). Transparency is flattened onto black. Nothing is written to disk."""
    src = entry.get("png") or entry.get("jpg") or entry.get("orig")
    if not src:
        raise Rejected("no picture file for that meme")
    try:
        from PIL import Image
    except ImportError:
        raise Rejected("Pillow is missing, can't make the small preview")
    import io
    with Image.open(src) as im:
        im.seek(0)
        if im.mode in ("RGBA", "LA", "P", "PA") or "transparency" in im.info:
            rgba = im.convert("RGBA")
            flat = Image.new("RGB", rgba.size, (0, 0, 0))
            flat.paste(rgba, mask=rgba.getchannel("A"))
        else:
            flat = im.convert("RGB")
    flat.thumbnail((PREVIEW_W, PREVIEW_H), Image.LANCZOS)
    canvas = Image.new("RGB", (PREVIEW_W, PREVIEW_H), (0, 0, 0))
    canvas.paste(flat, ((PREVIEW_W - flat.width) // 2, (PREVIEW_H - flat.height) // 2))
    buf = io.BytesIO()
    canvas.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


@mcp.tool()
def robot_show_meme(name: str = "latest") -> str:
    """Show a saved meme on her screen (centred, half size, about 5 seconds, then her face comes back).
    name: "latest" (default) for the meme just saved, or words from its title, e.g. "homework".
    Only memes saved with save_meme in the NousGirl-memes folder. Call it after save_meme when someone
    wants to see the meme. Keep your reply short."""
    def go():
        now = time.monotonic()
        if now - _last_meme[0] < MEME_MIN_INTERVAL_S:
            return _err("one meme at a time, wait a moment")
        stem = pick_meme(name)
        if stem is None:
            return _err("no saved meme matches that")
        data = meme_png_bytes(list_memes()[stem])
        if len(data) > MEME_MAX_BYTES:
            return _err("that meme file is too big for the screen")
        _last_meme[0] = now
        import base64
        raw = _transport_post("/hg/api/show_meme",
                              {"png_b64": base64.b64encode(data).decode("ascii"), "title": stem[16:]},
                              timeout=45.0)
        try:
            d = json.loads(raw or "{}")
        except ValueError:
            d = {"ok": False, "error": raw[:300]}
        if isinstance(d, dict):
            d["meme"] = stem
        return json.dumps(d)
    return _run(go)


@mcp.tool()
def robot_status() -> str:
    """Is she online right now, and which robot tools the voice path has."""
    raw = _transport_get("/hg/api/tools")
    online = False
    try:
        d = json.loads(raw)
        online = bool(d.get("online")) if isinstance(d, dict) else False
    except ValueError:
        pass
    return json.dumps({"online": online, "voice_tools": list(VOICE_TOOLS)})


if __name__ == "__main__":
    mcp.run()
