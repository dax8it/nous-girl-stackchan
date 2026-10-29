"""v3.6.7-rave2: voice rave mode for Nous-Girl (needs v3.6.7-dj4 firmware or later).

Called first from play_music.hg_try_music_shortcut (before radio, set lists and
next / previous), so a rave phrase never reaches the LLM.

  "rave mode", "rave mode on", "start a rave", "start to rave", "start raving", "let's rave", "rave time"
        -> robot: self.dj.mode rave_on (also turns DJ mode on: DJ screen, EQ, LED sync,
           dancing). Music: a set list or radio that is paused resumes ("continue"),
           one that is playing keeps playing, otherwise the next library song starts.
  "start a rave with Groove Salad", "rave mode, play My Song", "rave to <x>"
        -> rave_on, then the text becomes "play <x>" (radio station or library song).
  "rave mode off", "turn off rave mode", "end rave mode"
        -> rave_off: back to the normal DJ screen, the music keeps going.
  "stop the rave", "end the rave", "rave over"
        -> rave_off and the music stops ("stop" goes to the radio or set list).

hg_try_rave returns True (handled), a str (rewritten text for the rest of the music
shortcut), or None (not a rave phrase). Off with plugins.play_music.hg_voice_shortcut:
false (the caller checks it). Old firmware without rave_on just ignores the call.
"""
import re

TAG = __name__

_LEAD = r"^(?:(?:ok|okay|hey|um|uh|so|please|alright|yo|nous girl|new girl|noose girl)\s+)*"
_TAIL = r"(?:\s+(?:please|now|again|baby|girl))*$"
# "rave" is sometimes heard as "brave" / "raves" / "rave's"; "brave" only counts next to "mode".
_RAVE = r"(?:rave|raves|rave's|raive)"
_RAVE_M = r"(?:rave|raves|rave's|raive|brave)\s+mode"
_WHAT = r"(?P<what>[\w' &-]{2,60}?)"

_OFF_RE = re.compile(
    _LEAD + r"(?:"
    r"(?:" + _RAVE_M + r"|" + _RAVE + r")\s+off"
    r"|(?:turn|switch|shut)\s+off\s+(?:the\s+)?" + _RAVE_M +
    r"|(?:turn|switch|shut)\s+(?:the\s+)?" + _RAVE_M + r"\s+off"
    r"|(?:stop|end|exit|quit|leave|cancel|disable|no more)\s+(?:the\s+)?" + _RAVE_M +
    r"|no\s+more\s+" + _RAVE + r"\s+(?:screen|colors|colours|lights)"
    r")" + _TAIL)

_STOP_RE = re.compile(
    _LEAD + r"(?:"
    r"(?:stop|end|kill|finish|cancel)\s+(?:the\s+|this\s+|our\s+)?" + _RAVE + r"(?:\s+party)?"
    r"|(?:the\s+)?" + _RAVE + r"(?:\s+is)?\s+over"
    r"|(?:party|rave)'?s?\s+over"
    r")" + _TAIL)

_ON_CORE = (
    r"(?:(?:start|begin|turn on|switch on|switch to|go into|go to|enter|activate|enable|"
    r"do|let's do|lets do|let's have|lets have|let's go|let's start|lets start|time for|"
    r"put on|throw|have|we need)\s+)?(?:a\s+|the\s+|some\s+)?"
    r"(?:" + _RAVE_M + r"(?:\s+on)?|" + _RAVE + r"(?:\s+(?:time|party|on))?)"
    r"|(?:let's|lets|let us|time to|start to|begin to|going to|gonna|want to|wanna)\s+" + _RAVE +
    r"|(?:start|begin|go|let's go|lets go|let's start|lets start)\s+raving" +
    r"|" + _RAVE_M + r"\s+(?:activate|activated|go|start|engage)" +
    r"|" + _RAVE + r"\s+time"
)
_ON_RE = re.compile(_LEAD + r"(?:" + _ON_CORE + r")" + _TAIL)
_ON_WITH_RE = re.compile(
    _LEAD + r"(?:" + _ON_CORE + r")\s+(?:(?:and\s+)?(?:with|to|playing|play|put on|on)\s+)" + _WHAT + _TAIL)


def _norm(text):
    t = re.sub(r"[^\w\s'&-]", " ", text or "").lower()
    return re.sub(r"\s+", " ", t).strip()


def parse_rave(text):
    """-> (cmd, what). cmd: "on" | "off" | "stop" | None. what: song or station for "on"."""
    t = _norm(text)
    if not t or not any(w in t for w in ("rave", "raive", "raving")):
        return None, None
    if _OFF_RE.match(t):
        return "off", None
    if _STOP_RE.match(t):
        return "stop", None
    if _ON_RE.match(t):
        return "on", None
    m = _ON_WITH_RE.match(t)
    if m:
        what = re.sub(r"^(?:the\s+)?(?:song|track|station|radio station)\s+", "", m.group("what")).strip()
        what = re.sub(r"\s+(?:please|now)$", "", what).strip()
        if what in ("", "it", "music", "some music", "something"):
            return "on", None
        return "on", what
    return None, None


def _active(conn):
    """-> (kind, paused) of a radio or set list on this connection, or (None, False)."""
    st = getattr(conn, "hg_radio", None)
    if st is not None and not getattr(st, "done", False):
        return "radio", bool(getattr(st, "paused", False))
    st = getattr(conn, "hg_setlist", None)
    if st is not None and not getattr(st, "done", False):
        return "setlist", bool(getattr(st, "paused", False))
    return None, False


def _log(conn):
    try:
        return conn.logger.bind(tag=TAG)
    except Exception:
        import logging
        return logging.getLogger(TAG)


async def hg_try_rave(conn, text):
    cmd, what = parse_rave(text)
    if cmd is None:
        return None
    import plugins_func.functions.play_music as pm
    kind, paused = _active(conn)
    if cmd == "on":
        conn.hg_dj = True
        conn.hg_rave = True
        sent = pm._hg_device_tool(conn, "self_dj_mode", {"action": "rave_on"})
        _log(conn).info("rave on (device %s, %s%s, what=%r)" % (
            "ok" if sent else "no rave_on tool", kind or "nothing playing", " paused" if paused else "", what))
        if what:
            return "play " + what
        if kind and not paused:
            return True                    # already playing: just the screen changes
        if kind:
            return "continue"
        return "next song"
    pm._hg_device_tool(conn, "self_dj_mode", {"action": "rave_off"})
    conn.hg_rave = False
    _log(conn).info("rave %s (%s%s)" % (cmd, kind or "nothing playing", " paused" if paused else ""))
    if cmd == "off":
        if kind and paused:
            return "continue"
        if kind:
            return True
        await pm._hg_say(conn, "Rave off.")
        return True
    if kind:
        return "stop"                      # the radio / set list stops and answers
    await pm._hg_say(conn, "Rave over.")
    return True
