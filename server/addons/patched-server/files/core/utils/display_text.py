"""Device-bound display text sanitizer (Nous-Girl punct fix, 2026-10-24).

Her screen font (common noto-v1) may lack typographic punctuation; she rebooted
after replies with em dashes and curly/German quotes. Only text sent to the
device's screen goes through here. TTS input is untouched.
"""
import re
import unicodedata

_DASHES = "\u2014\u2013\u2012\u2015\u2212\u2010\u2011\u2043\ufe58\ufe63\uff0d"
_MAP = {}
for _c in "\u201c\u201d\u201e\u201f\u00ab\u00bb\u2033\u2036\u301d\u301e\uff02":
    _MAP[_c] = '"'
for _c in "\u2018\u2019\u201a\u201b\u2032\u2035\u2039\u203a\uff07":
    _MAP[_c] = "'"
for _c in "\u2022\u00b7\u2027\u2219\u25cf\u25e6\u2043":
    _MAP[_c] = "-"
_MAP["\u2026"] = "..."
_MAP["\u2044"] = "/"
_ZERO_WIDTH = "\u200b\u200c\u200d\u2060\ufeff\u00ad\u180e"

# dash between two words -> ", "; between digits -> "-"
_DASH_RE = re.compile(r"\s*[" + _DASHES + r"]+\s*")


def _dash(m):
    s, e = m.start(), m.end()
    src = m.string
    before = src[s - 1] if s > 0 else ""
    after = src[e] if e < len(src) else ""
    if before.isdigit() and after.isdigit():
        return "-"
    if before and after and (before.isalnum() or before in "\"')") and (after.isalnum() or after in "\"'(¡¿"):
        return ", "
    if not before:
        return "- " if after else "-"
    return " - " if after else ""


def sanitize_display_text(text):
    if not isinstance(text, str) or not text:
        return text
    out = []
    for ch in text:
        if ch in _ZERO_WIDTH:
            continue
        if ch in _MAP:
            out.append(_MAP[ch])
            continue
        cp = ord(ch)
        if cp > 0xFFFF:  # emoji and other astral symbols
            continue
        if cp < 0x80 or ch in _DASHES:
            out.append(ch)
            continue
        cat = unicodedata.category(ch)
        if cat.startswith("Z"):  # nbsp, thin, narrow, ideographic spaces
            out.append(" ")
        elif cat in ("Cf", "Cc", "Co", "Cs", "Cn"):
            continue
        elif cp > 0xFF and (cat.startswith("P") or cat in ("So", "Sk", "Sm")):
            continue  # unknown fancy punctuation/symbols beyond Latin-1
        else:
            out.append(ch)  # letters with accents, digits, Latin-1 incl. ¡ ¿
    s = _DASH_RE.sub(_dash, "".join(out))
    s = re.sub(r"[ \t]{2,}", " ", s)
    s = re.sub(r" +([,.!?;:])", r"\1", s)
    s = re.sub(r",\s*,", ",", s)
    s = re.sub(r",([.!?])", r"\1", s)
    return s.strip() if s.strip() != s and text.strip() == text else s
