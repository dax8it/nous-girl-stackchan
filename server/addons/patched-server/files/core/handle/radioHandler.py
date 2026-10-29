"""v3.6.7-radio: internet radio for Nous-Girl.

Stations: data/radio/stations.json (id, name, aliases, url, genre, by, notes). Order in the
file = rotation order for next / previous. Every station is in the rotation.

How audio gets to her: ffmpeg (already in the server image) pulls the station's stream URL
and decodes it to 16 kHz mono 16-bit PCM; this module cuts that into 60 ms frames, encodes
them with the same Opus settings play_music / DJ set lists use (opuslib_next, 16 kHz, mono,
APPLICATION_AUDIO) and pushes them through conn.tts.tts_audio_queue exactly like
setlistHandler does for songs. So the firmware sees ordinary "her speech channel" audio:
the DJ screen, 13-band EQ, beat flash and LED sync work unchanged.

Buffering is bounded: at most LEAD_S of audio is queued ahead of the speaker, ffmpeg's pipe
is read only when needed (pipe + reader limit ~2-3 s), and nothing is read while paused.

Timeouts: no audio within CONNECT_TIMEOUT_S of opening a station -> she says it isn't
answering and moves to the next station. A playing stream that stalls for STALL_TIMEOUT_S
or ends is reopened up to MAX_RECONNECTS times (counter resets after 60 s of good audio),
then skipped the same way. If no station answers she says so and stops.

Talking over it: a tap / wake word aborts her output (client_abort) and anything else she
says takes the speech channel (new sentence_id); both stop the stream and kill ffmpeg (no
background downloading). The radio stays "paused" for PAUSED_FORGET_S: "continue" /
"resume the radio" reopens the same station live; a swipe ("next song" / "previous song")
or "next station" changes station.

Voice (before the LLM; plugins.play_music.hg_voice_shortcut: false turns all of it off):
  "play the radio" / "turn on the radio" / "radio on"          -> last (or default) station
  "play groove salad" / "switch to jazz" / "radio paradise"     -> that station
  "next station" / "previous station" / "change the station"   -> rotation
  "stop the radio" / "radio off"                               -> stop
  "what stations do you have" / "list the stations"            -> she reads the names
  "what station is this"                                       -> she says the name
While the radio is on or paused: "next song" / "previous song" (the screen swipe sends
these) change station, "stop" ends it, "continue" resumes.
"""
import asyncio
import difflib
import json
import os
import re
import time
import uuid

from core.handle import sceneHandler as sh

TAG = __name__
STATIONS_FILE = "data/radio/stations.json"
FORMAT = "radio-stations/1"
FRAME_S = sh.FRAME_S          # 0.06 s opus frames (tests may shrink it)
SAMPLE_RATE = 16000
FRAME_SAMPLES = 960           # 60 ms at 16 kHz
FRAME_BYTES = FRAME_SAMPLES * 2
LEAD_S = 1.0                  # audio queued ahead of the speaker
CHUNK_FRAMES = 8              # 0.48 s per push
CONNECT_TIMEOUT_S = 8.0
STALL_TIMEOUT_S = 8.0
MAX_RECONNECTS = 2
GOOD_RESET_S = 60.0
PAUSED_FORGET_S = 600
GROOVE_REARM_S = 600          # firmware groove runs max 1800 s per call; re-arm well before
MAX_STATIONS = 40
USER_AGENT = "NousGirlRadio/1.0 (personal StackChan)"
_KEYS = {"id", "name", "aliases", "url", "genre", "by", "notes", "comment"}
_TOP = {"format", "comment", "default", "stations"}


class StationError(ValueError):
    pass


def _log(conn):
    return conn.logger.bind(tag=TAG)


def _norm(text):
    t = re.sub(r"[^\w\s']", " ", (text or "").replace("-", " ")).lower()
    return re.sub(r"\s+", " ", t).strip()


# ── stations file ────────────────────────────────────────────────────────────
def validate_stations(doc):
    """-> (stations list, default index). Raises StationError with a readable reason."""
    if isinstance(doc, (str, bytes)):
        try:
            doc = json.loads(doc)
        except ValueError as e:
            raise StationError("not JSON: %s" % e)
    if not isinstance(doc, dict):
        raise StationError("top level must be an object")
    if doc.get("format") != FORMAT:
        raise StationError("format must be %s" % FORMAT)
    extra = set(doc) - _TOP
    if extra:
        raise StationError("unknown top-level keys: %s" % ", ".join(sorted(extra)))
    raw = doc.get("stations")
    if not isinstance(raw, list) or not raw:
        raise StationError("stations must be a non-empty list")
    if len(raw) > MAX_STATIONS:
        raise StationError("at most %d stations" % MAX_STATIONS)
    out, ids = [], set()
    for i, s in enumerate(raw):
        where = "stations[%d]" % i
        if not isinstance(s, dict):
            raise StationError("%s must be an object" % where)
        extra = set(s) - _KEYS
        if extra:
            raise StationError("%s: unknown keys %s" % (where, ", ".join(sorted(extra))))
        sid = s.get("id")
        if not isinstance(sid, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,31}", sid):
            raise StationError("%s: id is required (a-z, 0-9, - or _)" % where)
        if sid in ids:
            raise StationError("%s: id %s used twice" % (where, sid))
        ids.add(sid)
        name = s.get("name")
        if not isinstance(name, str) or not name.strip():
            raise StationError("%s: name is required" % where)
        url = s.get("url")
        if not isinstance(url, str) or not re.fullmatch(r"https?://[^\s\"'<>]+", url):
            raise StationError("%s: url must be an http(s) stream URL" % where)
        aliases = s.get("aliases", [])
        if not isinstance(aliases, list) or not all(isinstance(a, str) and a.strip() for a in aliases):
            raise StationError("%s: aliases must be a list of words" % where)
        for k in ("genre", "by", "notes"):
            if k in s and not isinstance(s[k], str):
                raise StationError("%s: %s must be text" % (where, k))
        names = []
        for a in [name, sid] + aliases:
            n = _norm(a)
            if n and n not in names:
                names.append(n)
        out.append({"id": sid, "name": name.strip(), "url": url, "aliases": names,
                    "genre": s.get("genre", ""), "by": s.get("by", "")})
    d = doc.get("default", out[0]["id"])
    idx = next((i for i, s in enumerate(out) if s["id"] == d), None)
    if idx is None:
        raise StationError("default %r is not a station id" % d)
    return out, idx


def load_stations(path=None):
    path = path or STATIONS_FILE
    with open(path, encoding="utf-8") as fh:
        return validate_stations(fh.read())


def match_station(text, stations):
    """Station whose name / id / alias best matches `text` (already the station part), or None.
    Exact first, then a whole-word alias inside the text (longest wins), then close spelling."""
    full = re.sub(r"^(?:the\s+)", "", _norm(text))
    t = re.sub(r"\s+(?:radio|station|radio station|stream|channel)$", "", full).strip() or full
    if not t:
        return None
    for cand in (full, t):          # "agent radio" is a name; "drone zone radio" is "drone zone"
        for s in stations:
            if cand in s["aliases"]:
                return s
    t = full
    best, blen = None, 0
    for s in stations:
        for a in s["aliases"]:
            if len(a) > blen and re.search(r"(?:^|\s)" + re.escape(a) + r"(?:\s|$)", t):
                best, blen = s, len(a)
    if best is not None and blen >= 4:
        return best
    pool = {a: s for s in stations for a in s["aliases"] if len(a) >= 4}
    hit = difflib.get_close_matches(t, list(pool), n=1, cutoff=0.8)
    return pool[hit[0]] if hit else None


# ── voice ────────────────────────────────────────────────────────────────────
_LEAD = r"^(?:(?:ok|okay|hey|um|uh|so|please|nous girl|new girl|noose girl|can you|could you)\s+)*"
_TAIL = r"(?:\s+(?:please|now|again|for me))*$"
_RADIO = r"(?:the\s+|some\s+|a\s+)?(?:radio|internet radio)"
_ON_RE = re.compile(_LEAD + r"(?:(?:play|put on|turn on|switch on|start|listen to|i want|let's listen to|lets listen to)\s+"
                    + _RADIO + r"|radio\s+on|radio\s+time)" + _TAIL)
_OFF_RE = re.compile(_LEAD + r"(?:(?:stop|turn off|switch off|end|kill|cancel)\s+" + _RADIO + r"|radio\s+off|no more radio)" + _TAIL)
_NEXT_RE = re.compile(_LEAD + r"(?:(?:go to\s+|play\s+|try\s+)?(?:the\s+)?(?:next|another|a different|different)\s+(?:radio\s+)?station"
                      r"|(?:change|switch|skip)\s+(?:the\s+)?(?:radio\s+)?station|station\s+up)" + _TAIL)
_PREV_RE = re.compile(_LEAD + r"(?:(?:go back to\s+|go to\s+|play\s+)?(?:the\s+)?(?:previous|last|prior)\s+(?:radio\s+)?station"
                      r"|go back\s+(?:a|one)\s+station|station\s+down)" + _TAIL)
_LIST_RE = re.compile(_LEAD + r"(?:what|which)\s+(?:radio\s+)?stations\s+(?:do you have|are there|can you play|have you got)"
                      r"|" + _LEAD + r"(?:list|name|tell me)\s+(?:the\s+|your\s+|all\s+)?(?:radio\s+)?stations" + _TAIL)
_WHAT_RE = re.compile(_LEAD + r"(?:what|which)\s+(?:radio\s+)?station\s+is\s+(?:this|that|playing|on)"
                      r"|" + _LEAD + r"what(?:'s| is)\s+(?:this\s+|the\s+)?(?:radio\s+)?station" + _TAIL)
# "play X radio" / "switch to X" / "tune to X" / "radio X" / "put on X station"
_PLAY_RE = re.compile(_LEAD + r"(?:(?:play|put on|switch to|change to|go to|tune to|tune in to|tune into|listen to|turn on)\s+"
                      r"(?P<a>.+?)(?:\s+on the radio)?|radio\s+(?P<b>.+))" + _TAIL)
_RESUME_RE = re.compile(_LEAD + r"(?:continue|resume|keep playing|keep going|carry on|go on|unpause|play"
                        r"|resume\s+" + _RADIO + r"|back to\s+" + _RADIO + r"|turn\s+" + _RADIO + r"\s+back on)" + _TAIL)
_STOP_RE = re.compile(_LEAD + r"(?:stop|stop it|stop the music|stop playing|that's enough|enough|turn it off|quiet)" + _TAIL)
_SONG_NEXT_RE = re.compile(_LEAD + r"(?:(?:play|go to)\s+)?(?:the\s+)?next\s+(?:song|track|tune|one)|skip(?:\s+this)?(?:\s+song)?" + _TAIL)
_SONG_PREV_RE = re.compile(_LEAD + r"(?:(?:play|go to|go back to)\s+)?(?:the\s+)?(?:previous|last|prior)\s+(?:song|track|tune|one)" + _TAIL)


def parse_radio(text, stations, active=False):
    """-> (cmd, station-or-None). cmd: play | next | prev | stop | list | what | resume | None.
    `active` = the radio is on or paused, so plain "next song" / "stop" / "continue" belong to it.
    Pure (unit-tested)."""
    t = _norm(text)
    if not t:
        return None, None
    if _OFF_RE.match(t):
        return "stop", None
    if _LIST_RE.match(t):
        return "list", None
    if _NEXT_RE.match(t):
        return "next", None
    if _PREV_RE.match(t):
        return "prev", None
    if _WHAT_RE.match(t):
        return ("what", None) if active else ("list", None)
    if _ON_RE.match(t):
        return "play", None
    m = _PLAY_RE.match(t)
    if m:
        what = (m.group("a") or m.group("b") or "").strip()
        s = match_station(what, stations)
        if s is not None:
            return "play", s
    if active:
        if _SONG_NEXT_RE.match(t):
            return "next", None
        if _SONG_PREV_RE.match(t):
            return "prev", None
        if _STOP_RE.match(t):
            return "stop", None
        if _RESUME_RE.match(t):
            return "resume", None
    return None, None


# ── seams (tests replace these) ──────────────────────────────────────────────
async def _tts_packets(conn, sentence, lang):
    return await sh._tts_packets(conn, sentence, lang)


async def _device_call(conn, wire, args):
    try:   # fire-and-forget: a robot without the tool (or a failed call) never breaks the radio
        await sh._device(conn, wire, args)
    except Exception as e:
        _log(conn).info("radio: device call %s skipped: %r" % (wire, e))


async def _stt(conn, text):
    from core.handle.sendAudioHandle import send_stt_message
    await send_stt_message(conn, text)


def _encoder():
    import opuslib_next
    enc = opuslib_next.Encoder(SAMPLE_RATE, 1, opuslib_next.APPLICATION_AUDIO)
    return lambda pcm: enc.encode(pcm, FRAME_SAMPLES)


def ffmpeg_args(url):
    return ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
            "-user_agent", USER_AGENT,
            "-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "4",
            "-rw_timeout", str(int(STALL_TIMEOUT_S * 1_000_000)),
            "-i", url, "-vn", "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "s16le", "pipe:1"]


async def _open_stream(url):
    """-> object with .stdout (asyncio StreamReader) and kill() / wait()."""
    return await asyncio.create_subprocess_exec(
        *ffmpeg_args(url), stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL, limit=FRAME_BYTES * CHUNK_FRAMES * 2)


async def _close_stream(proc):
    if proc is None:
        return
    try:
        if proc.returncode is None:
            proc.kill()
        await asyncio.wait_for(proc.wait(), 3)
    except (ProcessLookupError, asyncio.TimeoutError, Exception):
        pass


# ── runtime ──────────────────────────────────────────────────────────────────
class RadioState:
    def __init__(self, stations, idx):
        self.stations = stations
        self.idx = idx
        self.task = None
        self.sid = None
        self.clock = 0.0
        self.paused = False
        self.paused_at = 0.0
        self.done = False
        self.proc = None
        self.started_sending = False
        self.log = []                 # (what, detail) for tests and the log
        self.frames = 0               # opus frames pushed (tests / stats)

    def station(self):
        return self.stations[self.idx]


def _radio(conn):
    """Current radio state, or None. A set list started after the radio wins (the radio
    ends any set list when it starts, so both existing means the set list is newer)."""
    st = getattr(conn, "hg_radio", None)
    if st is None:
        return None
    if st.done:
        conn.hg_radio = None
        return None
    if getattr(conn, "hg_setlist", None) is not None:
        _forget(conn, st, "a DJ set list started")
        return None
    if st.paused and time.time() - st.paused_at > PAUSED_FORGET_S:
        _forget(conn, st, "paused too long")
        return None
    return st


def _forget(conn, st, why):
    st.done = True
    _cancel(st)
    if getattr(conn, "hg_radio", None) is st:
        conn.hg_radio = None
    st.log.append(("end", why))


def _alive(conn, st):
    return getattr(conn, "hg_radio", None) is st and not st.done and conn.sentence_id == st.sid \
        and not getattr(conn, "client_abort", False)


def _put(conn, st, kind, packets, text=None):
    ST = sh._sentence_type()
    conn.tts.tts_audio_queue.put((getattr(ST, kind), packets, text, st.sid))


async def _wait_until(t):
    d = t - time.time()
    if d > 0:
        await asyncio.sleep(d)


async def _speak(conn, st, text):
    try:
        packets = await _tts_packets(conn, text, "en")
    except Exception as e:
        _log(conn).warning("radio TTS failed for '%s': %r" % (text, e))
        return _alive(conn, st)
    if not _alive(conn, st):
        return False
    _put(conn, st, "FIRST", packets, text)
    st.started_sending = True
    st.clock = max(time.time(), st.clock) + len(packets) * FRAME_S
    st.log.append(("say", text))
    return True


def _rearm_groove(conn):
    asyncio.create_task(_device_call(conn, "self_dance_groove", {"on": True, "max_seconds": 1800}))


async def _read_chunk(conn, st, proc, n, timeout):
    """-> bytes (exactly n), "stopped", "stall" or "eof". Checks liveness every 0.1 s."""
    read = asyncio.ensure_future(proc.stdout.readexactly(n))
    deadline = time.time() + timeout
    try:
        while True:
            done, _ = await asyncio.wait({read}, timeout=0.1)
            if done:
                try:
                    return read.result()
                except asyncio.IncompleteReadError:
                    return "eof"
            if not _alive(conn, st):
                return "stopped"
            if time.time() > deadline:
                return "stall"
    finally:
        if not read.done():
            read.cancel()
            try:
                await read
            except BaseException:
                pass


async def _pump(conn, st, station, encode):
    """Play one open attempt. -> ("stopped"|"stall"|"eof"|"noaudio", seconds of audio sent)."""
    proc = await _open_stream(station["url"])
    st.proc = proc
    sent = 0
    first = True
    last_groove = time.time()
    n = FRAME_BYTES * CHUNK_FRAMES
    try:
        while True:
            if not _alive(conn, st):
                return "stopped", sent * FRAME_S
            now = time.time()
            if st.clock - now >= LEAD_S:
                await asyncio.sleep(min(0.05, max(0.005, st.clock - now - LEAD_S)))
                continue
            got = await _read_chunk(conn, st, proc, n, CONNECT_TIMEOUT_S if first else STALL_TIMEOUT_S)
            if isinstance(got, str):
                if first and got in ("stall", "eof"):
                    return "noaudio", 0.0
                return got, sent * FRAME_S
            if not _alive(conn, st):
                return "stopped", sent * FRAME_S
            packets = [encode(got[i:i + FRAME_BYTES]) for i in range(0, n, FRAME_BYTES)]
            if first:
                first = False
                _log(conn).info("radio: %s on air" % station["name"])
                st.log.append(("on_air", station["id"]))
            kind = "MIDDLE" if st.started_sending else "FIRST"
            _put(conn, st, kind, packets, station["name"] if kind == "FIRST" else None)
            st.started_sending = True
            sent += len(packets)
            st.frames += len(packets)
            st.clock = max(time.time(), st.clock) + len(packets) * FRAME_S
            if time.time() - last_groove > GROOVE_REARM_S:
                last_groove = time.time()
                _rearm_groove(conn)
    finally:
        st.proc = None
        await _close_stream(proc)


async def _play_station(conn, st, encode):
    """-> "stopped" (tap / talk / stop) or "failed" (could not get or keep audio)."""
    station = st.station()
    reconnects = 0
    while True:
        why, secs = await _pump(conn, st, station, encode)
        if why == "stopped":
            return "stopped"
        if why == "noaudio":
            st.log.append(("noaudio", station["id"]))
            _log(conn).info("radio: %s gave no audio in %.0f s" % (station["name"], CONNECT_TIMEOUT_S))
            return "failed"
        if secs >= GOOD_RESET_S:
            reconnects = 0
        reconnects += 1
        st.log.append(("reconnect", "%s %s" % (station["id"], why)))
        _log(conn).info("radio: %s %s, reconnect %d/%d" % (station["name"], why, reconnects, MAX_RECONNECTS))
        if reconnects > MAX_RECONNECTS:
            return "failed"
        if not _alive(conn, st):
            return "stopped"


async def _run(conn, st, heard, intro):
    try:
        conn.client_abort = False
        st.sid = uuid.uuid4().hex
        conn.sentence_id = st.sid
        st.paused = False
        st.started_sending = False
        if heard:
            await _stt(conn, heard)
        st.clock = time.time()
        sh._groove(conn, True)
        _rearm_groove(conn)
        asyncio.create_task(_device_call(conn, "self_dj_mode", {"action": "track", "track": "Radio: " + st.station()["name"]}))
        encode = _encoder()
        tried = 0
        if intro and not await _speak(conn, st, intro):
            return _pause(conn, st)
        while not st.done:
            got = await _play_station(conn, st, encode)
            if got == "stopped":
                return _pause(conn, st)
            tried += 1
            dead = st.station()["name"]
            if tried >= len(st.stations):
                await _speak(conn, st, "None of my radio stations are answering right now. Try again later.")
                await _wait_until(st.clock)
                _finish(conn, st, "no station answered")
                return
            st.idx = (st.idx + 1) % len(st.stations)
            name = st.station()["name"]
            asyncio.create_task(_device_call(conn, "self_dj_mode", {"action": "track", "track": "Radio: " + name}))
            if not await _speak(conn, st, "%s isn't answering. Switching to %s." % (dead, name)):
                return _pause(conn, st)
    except asyncio.CancelledError:
        raise
    except Exception as e:
        import traceback
        _log(conn).error("radio error: %r %s" % (e, traceback.format_exc()))
        _finish(conn, st, "error")


def _pause(conn, st):
    if getattr(conn, "hg_radio", None) is st and not st.done:
        st.paused, st.paused_at = True, time.time()
        sh._groove(conn, False)
        st.log.append(("paused", st.station()["id"]))
        _log(conn).info("radio paused on %s" % st.station()["name"])


def _finish(conn, st, why):
    st.done = True
    try:
        if st.sid is not None and conn.sentence_id == st.sid:
            _put(conn, st, "LAST", [], None)
    except Exception:
        pass
    sh._groove(conn, False)
    if getattr(conn, "hg_radio", None) is st:
        conn.hg_radio = None
    st.log.append(("end", why))
    _log(conn).info("radio ended: %s" % why)


def _cancel(st):
    if st.task and not st.task.done() and st.task is not asyncio.current_task():
        st.task.cancel()


def _end_other_media(conn):
    old = getattr(conn, "hg_setlist", None)
    if old is not None:
        old.done = True
        try:
            from core.handle.setlistHandler import _cancel as sl_cancel
            sl_cancel(old)
        except Exception:
            pass
        conn.hg_setlist = None
    conn.hg_dj_pick = None


def _dj_screen(conn):
    cfg = (conn.config.get("plugins", {}) or {}).get("radio", {}) or {}
    if cfg.get("dj_screen", True):
        try:
            from plugins_func.functions import play_music as pm
            pm.hg_set_dj(conn, True)      # the EQ screen shows while she plays (same as set lists)
        except Exception:
            pass


def _start(conn, stations, idx, heard, intro, st=None):
    """(Re)start playback on stations[idx]; reuses the state when switching."""
    if st is None:
        old = getattr(conn, "hg_radio", None)
        if old is not None:
            _forget(conn, old, "replaced")
        _end_other_media(conn)
        st = RadioState(stations, idx)
        conn.hg_radio = st
        _dj_screen(conn)
    else:
        _cancel(st)
        st.done = False
        st.paused = False
        st.idx = idx
    conn.hg_radio_last = st.station()["id"]
    st.log.append(("play", st.station()["id"]))
    _log(conn).info("radio: %s (%s) by '%s'" % (st.station()["name"], st.station()["id"], heard))
    st.task = asyncio.create_task(_run(conn, st, heard, intro))
    return st


def _intro(station, switching=False):
    by = (" from %s" % station["by"]) if station.get("by") else ""
    return ("Switching to %s%s." if switching else "Here's %s%s.") % (station["name"], by)


def _last_idx(conn, stations, default_idx):
    want = getattr(conn, "hg_radio_last", None)
    return next((i for i, s in enumerate(stations) if s["id"] == want), default_idx)


async def radio_command(conn, cmd, station=None, heard=""):
    """Shared by voice and the bridge (Hermes robot_radio). station = id/name/alias text or a
    station dict. -> dict describing what happened (never raises for user errors)."""
    try:
        stations, default_idx = load_stations()
    except (StationError, OSError) as e:
        _log(conn).warning("radio stations file unusable: %s" % e)
        return {"ok": False, "error": "stations file unusable: %s" % e}
    st = _radio(conn)
    if cmd == "list":
        return {"ok": True, "stations": [{"id": s["id"], "name": s["name"], "genre": s["genre"]} for s in stations],
                "playing": st.station()["name"] if st and not st.paused else None}
    if cmd == "status":
        if st is None:
            return {"ok": True, "state": "off"}
        return {"ok": True, "state": "paused" if st.paused else "playing", "station": st.station()["name"],
                "id": st.station()["id"]}
    if cmd == "stop":
        if st is None:
            return {"ok": True, "state": "off", "note": "radio was not on"}
        name = st.station()["name"]
        was_playing = not st.paused
        _cancel(st)
        _finish(conn, st, "stopped")
        return {"ok": True, "state": "off", "stopped": name, "was_playing": was_playing}
    if cmd in ("next", "prev"):
        if st is None:
            i = _last_idx(conn, stations, default_idx)
            i = (i + (1 if cmd == "next" else -1)) % len(stations)
            st = _start(conn, stations, i, heard, _intro(stations[i]))
        else:
            cur = next((i for i, s in enumerate(stations) if s["id"] == st.station()["id"]), st.idx)
            i = (cur + (1 if cmd == "next" else -1)) % len(stations)
            st.stations = stations
            _start(conn, stations, i, heard, _intro(stations[i], switching=True), st=st)
        return {"ok": True, "state": "playing", "station": stations[i]["name"], "id": stations[i]["id"]}
    if cmd == "resume":
        if st is None:
            cmd = "play"
        else:
            if not st.paused:
                return {"ok": True, "state": "playing", "station": st.station()["name"], "id": st.station()["id"]}
            _start(conn, st.stations, st.idx, heard, None, st=st)
            return {"ok": True, "state": "playing", "station": st.station()["name"], "id": st.station()["id"]}
    if cmd == "play":
        s = station
        if isinstance(s, str):
            s = match_station(s, stations) if s.strip() else None
            if s is None and station.strip():
                return {"ok": False, "error": "no station like %r" % station,
                        "stations": [x["name"] for x in stations]}
        if s is None:
            i = st.idx if st is not None else _last_idx(conn, stations, default_idx)
        else:
            i = next(j for j, x in enumerate(stations) if x["id"] == s["id"])
        if st is not None:
            switching = st.station()["id"] != stations[i]["id"]
            st.stations = stations
            _start(conn, stations, i, heard, _intro(stations[i], switching=switching), st=st)
        else:
            _start(conn, stations, i, heard, _intro(stations[i]))
        return {"ok": True, "state": "playing", "station": stations[i]["name"], "id": stations[i]["id"]}
    return {"ok": False, "error": "unknown radio command %r" % cmd}


def stop_for_other_media(conn):
    """play_music calls this before a server song starts: the radio ends quietly."""
    st = getattr(conn, "hg_radio", None)
    if st is not None:
        _forget(conn, st, "a song started")


async def hg_try_radio(conn, text):
    """True if this utterance belongs to the radio (so no LLM)."""
    try:
        return await _hg_try_radio(conn, text)
    except Exception as e:
        import traceback
        _log(conn).error("radio error: %r %s" % (e, traceback.format_exc()))
        return False


async def _say_short(conn, text, heard):
    await sh._say(conn, [text], heard, "en")


async def _hg_try_radio(conn, text):
    try:
        stations, _ = load_stations()
    except (StationError, OSError):
        return False
    st = _radio(conn)
    cmd, station = parse_radio(text, stations, active=st is not None)
    if cmd is None:
        return False
    if cmd == "list":
        names = [s["name"] for s in stations]
        line = "My radio stations are " + ", ".join(names[:-1]) + ", and " + names[-1] + "."
        if st is not None and not st.paused:
            _cancel(st)
            _pause(conn, st)
        await _say_short(conn, line + " Say the name and I'll put it on.", text)
        return True
    if cmd == "what":
        s = st.station()
        await _say_short(conn, "This is %s%s." % (s["name"], (", " + s["genre"]) if s["genre"] else ""), text)
        if not st.paused:
            _pause(conn, st)
        return True
    if cmd == "stop":
        if st is None:
            await _say_short(conn, "The radio isn't on.", text)
            return True
        await radio_command(conn, "stop", heard=text)
        await _say_short(conn, "Radio off.", text)
        return True
    await radio_command(conn, cmd, station, heard=text)
    return True
