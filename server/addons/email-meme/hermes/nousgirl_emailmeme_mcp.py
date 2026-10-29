#!/usr/bin/env python3
"""nousgirl_emailmeme (email-meme pack v1): ONE MCP tool for Nous-Girl, email_meme.

Emails a meme already saved on your computer (~/NousGirl-memes) as a JPEG attachment (full original size,
converted from the WebP original; Gmail and every mail app preview JPEG) from her own AgentMail inbox,
through the AgentMail REST API: POST /v0/inboxes/{inbox_id}/messages/send (attachments = base64 content).

Two steps, ALWAYS (she reads the address back first, even with one approved address):
  1. email_meme(name, to)               -> sends NOTHING. Returns the recipient + meme title for her to read back,
                                           and remembers that read-back for 5 minutes.
  2. email_meme(name, to, confirm=True) -> after the person says yes. Sends only if it matches a read-back
                                           (same meme, same address) from the last 5 minutes; otherwise it
                                           just returns a fresh read-back. Each read-back is good for ONE send.
Recipients: ONLY addresses in ~/.config/nousgirl/email-meme-allowlist.txt (outside the repo; you fill it).
  0 approved -> refuses. 1 approved -> `to` may be left out. 2+ -> `to` must name one (address or its label).
Limits: 10 sends per rolling hour, attachment <= 4 MB (AgentMail's whole request limit is 6 MB).
Log: one line per send/failure in ~/NousGirl-memes/email-log.txt (no key, no message body).
Key: AGENTMAIL_API_KEY read at call time from the nous-girl profile .env (Hermes strips keys from MCP envs);
  held in memory only, never returned, logged or printed; scrubbed from any error text.
No email addresses live in this file: the sending inbox is discovered via GET /v0/inboxes (the nousgirl* one)
  unless NG_EMAIL_INBOX is set.
Test hooks (env): NG_MEMES_DIR NG_EMAIL_ALLOWLIST NG_EMAIL_STATE_DIR NG_EMAIL_LOG NG_ENV_FILE NG_AGENTMAIL_BASE
  NG_EMAIL_INBOX NG_EMAIL_RATE.   Tests: the pack's tests/test_emailmeme.py (mock AgentMail, no real send).
"""
import base64, io, json, os, re, sys, time, urllib.error, urllib.parse, urllib.request
from datetime import datetime

CONFIRM_WINDOW_S = 300
MAX_ATTACH = 4 * 1024 * 1024
MAX_SIDE = 4096
NOTE_MAX = 400
MEME_STAMP = re.compile(r"^(\d{8}-\d{6})-(.+?)(-320x240)?\.(png|jpg|jpeg|webp|gif)$", re.I)
EMAIL_RE = re.compile(r"^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$")
LATEST = (["latest"], ["last"], ["newest"], ["recent"], ["it"], ["that"], ["that", "meme"], ["the", "meme"])


def _e(k, d):
    return os.path.expanduser(os.environ.get(k, d))


def paths():
    memes = _e("NG_MEMES_DIR", "~/NousGirl-memes")
    state = _e("NG_EMAIL_STATE_DIR", "~/.config/nousgirl")
    return {"memes": memes, "allow": _e("NG_EMAIL_ALLOWLIST", "~/.config/nousgirl/email-meme-allowlist.txt"),
            "pending": os.path.join(state, "email-meme-pending.json"), "state": state,
            "log": _e("NG_EMAIL_LOG", os.path.join(memes, "email-log.txt")),
            "env": _e("NG_ENV_FILE", "~/.hermes/profiles/nous-girl/.env")}


def rate_limit():
    try:
        return max(1, int(os.environ.get("NG_EMAIL_RATE", "10")))
    except ValueError:
        return 10


# ---------------------------------------------------------------- allowlist
def load_allowlist(path=None):
    """[(label, address)], problems. Lines: `address` or `label = address`; # comments; case-insensitive."""
    path = path or paths()["allow"]
    out, problems = [], []
    try:
        st = os.stat(path)
    except OSError:
        return [], ["no allowlist file"]
    if st.st_mode & 0o022:
        return [], ["allowlist file is writable by other users (run: chmod 600 on it)"]
    for n, raw in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        label, addr = "", line
        if "=" in line:
            label, addr = (x.strip() for x in line.split("=", 1))
        addr = addr.strip("<>").strip().lower()
        if not EMAIL_RE.match(addr):
            problems.append("line %d is not an email address" % n); continue
        if addr not in [a for _, a in out]:
            out.append((label.lower(), addr))
    return out, problems


def pick_recipient(allow, to):
    """(address, None) or (None, result-dict)."""
    if not allow:
        return None, {"status": "refused", "reason": "no_approved_addresses",
                      "say": "I can't email memes yet: nobody is on my approved list. the owner has to add an address "
                             "on the computer first."}
    want = (to or "").strip().strip("<>").lower()
    if not want:
        if len(allow) == 1:
            return allow[0][1], None
        return None, {"status": "need_recipient", "choices": [l or a for l, a in allow],
                      "say": "Which approved address should I send it to?"}
    for label, addr in allow:
        if want == addr or (label and want == label):
            return addr, None
    return None, {"status": "refused", "reason": "not_approved",
                  "say": "I can only email memes to approved addresses, and that one isn't on my list."}


# ---------------------------------------------------------------- memes
def list_memes(folder):
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
        if m.group(3):
            e["png" if ext == "png" else "jpg"] = p
        else:
            e["orig"] = p
        e["mtime"] = max(e["mtime"], os.path.getmtime(p))
    return out


def pick_meme(name, folder):
    memes = list_memes(folder)
    if not memes:
        return None, memes
    order = sorted(memes, key=lambda s: (memes[s]["mtime"], s), reverse=True)
    words = re.findall(r"[a-z0-9]+", (name if isinstance(name, str) else "").lower())[:6]
    if not words or words in LATEST:
        return order[0], memes
    for s in order:
        if all(w in s.lower() for w in words):
            return s, memes
    return None, memes


def title_of(stem):
    slug = stem.split("-", 2)[2] if stem.count("-") >= 2 else stem
    t = re.sub(r"[-_]+", " ", slug).strip()
    return (t[:1].upper() + t[1:]) if t else "meme"


def jpeg_attachment(entry):
    """Full-size JPEG of the original (WebP/PNG/JPG), shrunk only if over MAX_SIDE or MAX_ATTACH."""
    from PIL import Image
    src = entry.get("orig") or entry.get("png") or entry.get("jpg")
    if not src:
        raise ValueError("no picture file for that meme")
    with Image.open(src) as im:
        im.seek(0)
        if im.mode in ("RGBA", "LA", "P"):
            im = im.convert("RGBA")
            bg = Image.new("RGB", im.size, (255, 255, 255)); bg.paste(im, mask=im.split()[-1]); im = bg
        else:
            im = im.convert("RGB")
        if max(im.size) > MAX_SIDE:
            im.thumbnail((MAX_SIDE, MAX_SIDE))
        for q, side in ((92, None), (85, None), (85, 2048), (80, 1600), (75, 1200)):
            if side and max(im.size) > side:
                im.thumbnail((side, side))
            buf = io.BytesIO(); im.save(buf, format="JPEG", quality=q, optimize=True)
            data = buf.getvalue()
            if len(data) <= MAX_ATTACH:
                return data, im.size
    raise ValueError("meme picture is too big to email")


# ---------------------------------------------------------------- log, rate, pending
def _log(line):
    p = paths()["log"]
    try:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        fd = os.open(p, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
        with os.fdopen(fd, "a") as f:
            f.write(datetime.now().astimezone().isoformat(timespec="seconds") + "\t" + line + "\n")
    except OSError:
        pass


def sends_last_hour(now=None):
    now = now or time.time(); n = 0
    try:
        for line in open(paths()["log"], encoding="utf-8", errors="replace"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) > 1 and parts[1] == "SENT":
                try:
                    if now - datetime.fromisoformat(parts[0]).timestamp() < 3600:
                        n += 1
                except ValueError:
                    pass
    except OSError:
        pass
    return n


def _pending_load():
    try:
        return json.load(open(paths()["pending"]))
    except (OSError, ValueError):
        return None


def _pending_save(d):
    p = paths()
    os.makedirs(p["state"], mode=0o700, exist_ok=True)
    tmp = p["pending"] + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(d, f)
    os.replace(tmp, p["pending"])


def _pending_clear():
    try:
        os.remove(paths()["pending"])
    except OSError:
        pass


# ---------------------------------------------------------------- AgentMail
def api_key():
    try:
        for line in open(paths()["env"], encoding="utf-8", errors="replace"):
            line = line.strip()
            if line.startswith("export "):
                line = line[7:].strip()
            if line.startswith("AGENTMAIL_API_KEY="):
                v = line.split("=", 1)[1].strip().strip('"').strip("'")
                return v or None
    except OSError:
        pass
    return None


def _http(method, path, key, body=None):
    base = os.environ.get("NG_AGENTMAIL_BASE", "https://api.agentmail.to/v0").rstrip("/")
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(base + path, method=method, data=data, headers={
        "Authorization": "Bearer " + key, "Content-Type": "application/json", "Accept": "application/json",
        "User-Agent": "nousgirl-emailmeme/1"})
    try:
        with urllib.request.urlopen(r, timeout=45) as resp:
            return resp.status, json.loads(resp.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")[:300].replace(key, "<redacted>")
    except (urllib.error.URLError, OSError, ValueError) as e:
        return 0, str(e)[:200].replace(key, "<redacted>")


_inbox_cache = {}


def sending_inbox(key):
    fixed = os.environ.get("NG_EMAIL_INBOX", "").strip()
    if fixed:
        return fixed
    base = os.environ.get("NG_AGENTMAIL_BASE", "")
    if base in _inbox_cache:
        return _inbox_cache[base]
    st, data = _http("GET", "/inboxes", key)
    if st != 200 or not isinstance(data, dict):
        raise RuntimeError("could not list inboxes (HTTP %s)" % st)
    boxes = data.get("inboxes") or []
    ids = [(b.get("inbox_id") or b.get("email") or "") for b in boxes]
    mine = [i for i in ids if i.split("@")[0].replace("-", "") == "nousgirl"]
    pick = mine[0] if len(mine) == 1 else (ids[0] if len(ids) == 1 else None)
    if not pick:
        raise RuntimeError("could not tell which inbox is hers (%d inboxes)" % len(ids))
    _inbox_cache[base] = pick
    return pick


def send_via_agentmail(to, subject, text, filename, jpeg):
    key = api_key()
    if not key:
        raise RuntimeError("AGENTMAIL_API_KEY is not in her .env")
    inbox = sending_inbox(key)
    body = {"to": [to], "subject": subject, "text": text,
            "attachments": [{"filename": filename, "content_type": "image/jpeg",
                             "content_disposition": "attachment",
                             "content": base64.b64encode(jpeg).decode("ascii")}]}
    st, data = _http("POST", "/inboxes/%s/messages/send" % urllib.parse.quote(inbox, safe="@."), key, body)
    if st != 200 or not isinstance(data, dict) or not data.get("message_id"):
        raise RuntimeError("AgentMail said HTTP %s: %s" % (st, str(data)[:200]))
    return data.get("message_id")


# ---------------------------------------------------------------- the tool
def email_meme_impl(name="latest", to=None, note=None, confirm=False, sender=None):
    sender = sender or send_via_agentmail
    p = paths()
    allow, problems = load_allowlist(p["allow"])
    if problems and not allow:
        r = {"status": "refused", "reason": "no_approved_addresses",
             "say": "I can't email memes yet: my approved address list isn't set up. the owner has to add an address "
                    "on the computer first."}
        if "writable" in problems[0]:
            r["say"] = "I can't email memes right now: my approved address list has unsafe permissions."
        return r
    addr, err = pick_recipient(allow, to)
    if err:
        return err
    stem, memes = pick_meme(name, p["memes"])
    if not stem:
        return {"status": "refused", "reason": "no_meme",
                "say": "I couldn't find that meme. Want me to make one first?" if memes else
                       "There are no saved memes yet. Want me to make one first?"}
    title = title_of(stem)
    if sends_last_hour() >= rate_limit():
        return {"status": "refused", "reason": "rate_limit",
                "say": "I've already emailed %d memes this hour. Let's wait a bit." % rate_limit()}
    try:
        jpeg, size = jpeg_attachment(memes[stem])
    except Exception as e:   # noqa: BLE001 - tell her, never crash the server
        return {"status": "refused", "reason": "picture", "say": "I couldn't attach that meme (%s)." % str(e)[:80]}
    note = (note or "").strip()[:NOTE_MAX]
    now = time.time()
    readback = {"status": "confirm_needed", "sent": False, "to": addr, "meme_title": title,
                "say": "I'll email the meme \"%s\" to %s. Should I send it?" % (title, addr),
                "next": "Read this back. Only after they say yes, call email_meme again with the same name and "
                        "to=\"%s\" and confirm=true. If they say no, don't call it again." % addr}
    pend = _pending_load()
    fresh = bool(pend) and pend.get("stem") == stem and pend.get("to") == addr and \
        0 <= now - float(pend.get("created", 0)) <= CONFIRM_WINDOW_S
    if not confirm or not fresh:
        _pending_save({"stem": stem, "to": addr, "created": now})
        if confirm:
            readback["why"] = "No matching read-back in the last 5 minutes, so nothing was sent. Read it back first."
        return readback
    _pending_clear()                       # one read-back = one send, even if the send fails
    subject = "Meme from Nous-Girl: %s" % title
    text = "Here's a meme from Nous-Girl: %s\n\n%s%s- Nous-Girl\n" % (title, note, "\n\n" if note else "")
    fname = stem + ".jpg"
    try:
        mid = sender(addr, subject, text, fname, jpeg)
    except Exception as e:   # noqa: BLE001
        _log("FAILED\tto=%s\tmeme=%s\t%s" % (addr, stem, str(e)[:160].replace("\t", " ").replace("\n", " ")))
        return {"status": "failed", "sent": False, "say": "Sorry, the email didn't go through. Try again later.",
                "error": str(e)[:200]}
    _log("SENT\tto=%s\tmeme=%s\tbytes=%d\tsize=%dx%d\tmessage_id=%s" % (addr, stem, len(jpeg), size[0], size[1], mid))
    return {"status": "sent", "sent": True, "to": addr, "meme_title": title, "attachment": fname,
            "say": "Sent! The meme \"%s\" is on its way to %s." % (title, addr)}


def main():
    from typing import Optional
    from mcp.server.fastmcp import FastMCP
    mcp = FastMCP("nousgirl_emailmeme")

    @mcp.tool()
    def email_meme(name: str = "latest", to: Optional[str] = None, note: Optional[str] = None,
                   confirm: bool = False) -> str:
        """Email a meme saved on your computer (NousGirl-memes) as a picture attachment, from your own inbox.
        ONLY to approved addresses (the owner's list on your computer); it refuses anyone else.
        ALWAYS two steps: first call WITHOUT confirm: nothing is sent; it returns who it's going to and the
        meme title. Read that back and ask "Should I send it?". Only after a clear yes, call again with the
        same name and to (use the returned "to") and confirm=true. Never set confirm=true without that yes.
        name: "latest" (default) or words from the meme title, e.g. "homework". to: leave out if there is only
        one approved address; otherwise the address or its name on the list. note: optional short line."""
        return json.dumps(email_meme_impl(name, to, note, bool(confirm)))

    mcp.run()


if __name__ == "__main__":
    main()
