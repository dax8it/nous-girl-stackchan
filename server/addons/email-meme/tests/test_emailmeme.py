#!/usr/bin/env python3
"""Offline tests for nousgirl_emailmeme_mcp.py. A MOCK AgentMail server on 127.0.0.1 receives every "send";
nothing reaches the real AgentMail, no real key is read (a fake .env in a temp dir), ~/NousGirl-memes and
~/.config/nousgirl are never touched.  Run:  uv run --no-project --python 3.12 --with 'mcp>=1.2,<2' --with pillow \
python tests/test_emailmeme.py [path/to/nousgirl_emailmeme_mcp.py]"""
import asyncio, base64, importlib.util, io, json, os, re, stat, sys, tempfile, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
SRV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "hermes", "nousgirl_emailmeme_mcp.py")
FAKE_KEY = "fake-test-key-" + "0123456789abcdef"
DOM = "example" + ".invalid"
A1, A2, BAD = "parent@" + DOM, "Other.Parent@" + DOM, "stranger@" + DOM
INBOX = "nousgirl@mock" + ".invalid"
SENT, CALLS = [], []
MODE = {"fail": False}


class Mock(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _out(self, code, obj):
        b = json.dumps(obj).encode() if not isinstance(obj, (bytes, str)) else (obj.encode() if isinstance(obj, str) else obj)
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)

    def do_GET(self):
        CALLS.append(("GET", self.path, self.headers.get("Authorization")))
        if self.headers.get("Authorization") != "Bearer " + FAKE_KEY:
            return self._out(401, {"name": "Unauthorized"})
        if self.path == "/v0/inboxes":
            return self._out(200, {"count": 2, "inboxes": [{"inbox_id": "other@" + "mock.invalid"}, {"inbox_id": INBOX}]})
        self._out(404, {"name": "NotFound"})

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0")); body = json.loads(self.rfile.read(n) or b"{}")
        CALLS.append(("POST", self.path, self.headers.get("Authorization")))
        if self.headers.get("Authorization") != "Bearer " + FAKE_KEY:
            return self._out(401, {"name": "Unauthorized"})
        if MODE["fail"]:
            return self._out(500, "server error, your key was " + FAKE_KEY)
        if not re.fullmatch(r"/v0/inboxes/[^/]+/messages/send", self.path):
            return self._out(404, {"name": "NotFound"})
        SENT.append({"path": self.path, "body": body})
        self._out(200, {"message_id": "msg-%d" % len(SENT), "thread_id": "thr-1"})


passed = failed = 0


def ok(c, what):
    global passed, failed
    if c:
        passed += 1; print("  ok   " + what)
    else:
        failed += 1; print("  FAIL " + what)


def main():
    from PIL import Image
    srv = ThreadingHTTPServer(("127.0.0.1", 0), Mock); threading.Thread(target=srv.serve_forever, daemon=True).start()
    T = tempfile.mkdtemp(prefix="emailmeme-test-")
    memes, state = os.path.join(T, "memes"), os.path.join(T, "state"); os.makedirs(memes)
    envf, allow = os.path.join(T, "fake.env"), os.path.join(T, "allow.txt")
    open(envf, "w").write("OTHER=1\nAGENTMAIL_API_KEY=\"%s\"\n" % FAKE_KEY)
    os.environ.update(NG_MEMES_DIR=memes, NG_EMAIL_STATE_DIR=state, NG_EMAIL_ALLOWLIST=allow, NG_ENV_FILE=envf,
                      NG_AGENTMAIL_BASE="http://127.0.0.1:%d/v0" % srv.server_port, NG_EMAIL_RATE="10")
    os.environ.pop("NG_EMAIL_INBOX", None); os.environ.pop("NG_EMAIL_LOG", None)
    spec = importlib.util.spec_from_file_location("em", SRV); em = importlib.util.module_from_spec(spec); spec.loader.exec_module(em)

    def meme(stamp, slug, size=(750, 500), color=(200, 30, 30)):
        Image.new("RGB", size, color).save(os.path.join(memes, "%s-%s.webp" % (stamp, slug)), "WEBP")
        Image.new("RGB", (320, 240), color).save(os.path.join(memes, "%s-%s-320x240.png" % (stamp, slug)))
        t = time.mktime(time.strptime(stamp, "%Y%m%d-%H%M%S"))
        for f in os.listdir(memes):
            if f.startswith(stamp):
                os.utime(os.path.join(memes, f), (t, t))
    def setallow(text, mode=0o600):
        open(allow, "w").write(text); os.chmod(allow, mode)
    def call(**k):
        return em.email_meme_impl(**k)

    print("allowlist")
    r = call(); ok(r["status"] == "refused" and r["reason"] == "no_approved_addresses", "no allowlist file -> refused")
    setallow("# Approved addresses for email_meme, one per line\n\n")
    r = call(confirm=True); ok(r["status"] == "refused" and r["reason"] == "no_approved_addresses", "empty (comment only) -> refused, even with confirm")
    setallow(A1 + "\n", 0o666)
    r = call(); ok(r["status"] == "refused" and "permissions" in r["say"], "world-writable allowlist -> refused")
    setallow("not-an-address\n" + A1.upper() + "  # me\n")
    al, probs = em.load_allowlist(allow); ok(al == [("", A1)] and len(probs) == 1, "parses address, lowercases, skips junk line")

    print("memes")
    r = call(); ok(r["status"] == "refused" and r["reason"] == "no_meme", "no memes saved -> refused")
    meme("20261028-144650", "homework-versus-one-quick-match")
    meme("20261028-153457", "robots-making-memes", size=(511, 800), color=(10, 120, 200))
    ok(em.pick_meme("latest", memes)[0] == "20261028-153457-robots-making-memes", "latest = newest")
    ok(em.pick_meme("that meme", memes)[0] == "20261028-153457-robots-making-memes", "'that meme' = newest")
    ok(em.pick_meme("homework", memes)[0] == "20261028-144650-homework-versus-one-quick-match", "title words pick the right one")
    r = call(name="dinosaur pizza"); ok(r["reason"] == "no_meme", "unknown title -> refused")

    print("two-step read-back (one approved address)")
    r = call(); ok(r["status"] == "confirm_needed" and r["to"] == A1 and r["sent"] is False and not SENT,
                   "step 1 (no confirm): read-back only, nothing sent")
    ok("Robots making memes" in r["say"] and A1 in r["say"] and "Should I send it?" in r["say"], "read-back names meme + address")
    pm = stat.S_IMODE(os.stat(os.path.join(state, "email-meme-pending.json")).st_mode); ok(pm == 0o600, "pending file mode 0600")
    r = call(confirm=True); ok(r["status"] == "sent" and len(SENT) == 1, "step 2 (confirm after read-back): sent once to MOCK")
    b = SENT[0]["body"]
    ok(SENT[0]["path"] == "/v0/inboxes/%s/messages/send" % INBOX, "POST /v0/inboxes/{nousgirl inbox}/messages/send (inbox discovered)")
    ok(b["to"] == [A1] and b["subject"] == "Meme from Nous-Girl: Robots making memes", "to + subject")
    att = b["attachments"][0]; raw = base64.b64decode(att["content"], validate=True)
    im = Image.open(io.BytesIO(raw))
    ok(att["content_type"] == "image/jpeg" and att["filename"].endswith(".jpg") and im.format == "JPEG", "attachment is base64 JPEG")
    ok(im.size == (511, 800), "attachment is the FULL-size original (511x800), not the 320x240")
    ok(all(c[2] == "Bearer " + FAKE_KEY for c in CALLS), "Bearer auth header")
    r = call(confirm=True); ok(r["status"] == "confirm_needed" and len(SENT) == 1, "read-back is single-use: 2nd confirm re-reads, no send")
    em._pending_clear()
    r = call(confirm=True); ok(r["status"] == "confirm_needed" and "why" in r and len(SENT) == 1, "confirm with NO read-back -> no send")
    call(); p = json.load(open(os.path.join(state, "email-meme-pending.json"))); p["created"] -= 400
    json.dump(p, open(os.path.join(state, "email-meme-pending.json"), "w"))
    r = call(confirm=True); ok(r["status"] == "confirm_needed" and len(SENT) == 1, "read-back older than 5 min -> no send")
    call(name="homework")
    r = call(name="latest", confirm=True); ok(r["status"] == "confirm_needed" and len(SENT) == 1, "read-back for meme A, confirm for meme B -> no send")
    ok(json.load(open(os.path.join(state, "email-meme-pending.json")))["stem"].endswith("robots-making-memes"),
       "a mismatched confirm replaces the read-back with a fresh one for what was asked")
    call(name="homework")
    r = call(name="homework", confirm=True, note="for your fridge")
    ok(r["status"] == "sent" and SENT[-1]["body"]["attachments"][0]["filename"].startswith("20261028-144650") and
       "for your fridge" in SENT[-1]["body"]["text"], "read-back + confirm by title words -> sent, note in body")
    r = call(to=BAD); ok(r["status"] == "refused" and r["reason"] == "not_approved", "address not on list -> refused")
    r = call(to=BAD, confirm=True); ok(r["status"] == "refused" and len(SENT) == 2, "not on list + confirm -> refused, no send")

    print("several approved addresses")
    setallow("dad = %s\n%s\n" % (A1, A2))
    r = call(); ok(r["status"] == "need_recipient" and r["choices"] == ["dad", A2.lower()], "no `to` -> asks which (choices)")
    r = call(to="DAD"); ok(r["status"] == "confirm_needed" and r["to"] == A1, "label match (case-insensitive)")
    r = call(to=A2.upper(), ); ok(r["to"] == A2.lower(), "address match (case-insensitive)")
    r = call(to=A2, confirm=True); ok(r["status"] == "sent" and SENT[-1]["body"]["to"] == [A2.lower()], "confirm -> sent to the chosen one")
    call(to="dad"); r = call(to=A2, confirm=True); ok(r["status"] == "confirm_needed" and len(SENT) == 3, "read-back for dad, confirm for other -> no send")

    print("errors, rate limit, size, log")
    setallow(A1 + "\n")
    MODE["fail"] = True; call(); r = call(confirm=True); MODE["fail"] = False
    ok(r["status"] == "failed" and FAKE_KEY not in json.dumps(r) and "<redacted>" in r["error"], "API error -> failed, key scrubbed")
    open(envf, "w").write("OTHER=1\n"); call(); r = call(confirm=True)
    ok(r["status"] == "failed" and "AGENTMAIL_API_KEY" in r["error"], "no key in .env -> failed cleanly")
    open(envf, "w").write("export AGENTMAIL_API_KEY='%s'\n" % FAKE_KEY)
    os.environ["NG_EMAIL_RATE"] = "4"
    call(); r = call(confirm=True); ok(r["status"] == "sent" and len(SENT) == 4, "4th send ok (export/quoted .env line)")
    r = call(); ok(r["status"] == "refused" and r["reason"] == "rate_limit", "rate limit: 5th within the hour refused")
    os.environ["NG_EMAIL_RATE"] = "10"
    log = os.path.join(memes, "email-log.txt"); lt = open(log).read()
    ok(stat.S_IMODE(os.stat(log).st_mode) == 0o600 and lt.count("\tSENT\t") == 4 and lt.count("\tFAILED\t") == 2,
       "email-log.txt: 0600, 4 SENT + 2 FAILED lines")
    ok(FAKE_KEY not in lt and "message_id=msg-" in lt, "log has message ids and no key")
    rng = os.urandom(5200 * 3600 * 3)
    Image.frombytes("RGB", (5200, 3600), rng).save(os.path.join(memes, "20261028-160000-noise.webp"), "WEBP", quality=100)
    data, size = em.jpeg_attachment(em.list_memes(memes)["20261028-160000-noise"])
    ok(len(data) <= em.MAX_ATTACH and max(size) <= em.MAX_SIDE, "huge meme shrunk to <= 4 MB / 4096 px (%d KB, %dx%d)" % (len(data) // 1024, *size))

    print("MCP stdio server")
    async def mcpcheck():
        from mcp import ClientSession, StdioServerParameters
        from mcp.client.stdio import stdio_client
        prm = StdioServerParameters(command=sys.executable, args=[SRV], env=dict(os.environ))
        async with stdio_client(prm) as (r_, w_):
            async with ClientSession(r_, w_) as s:
                await s.initialize(); tl = await s.list_tools()
                names = [t.name for t in tl.tools]; props = tl.tools[0].inputSchema.get("properties", {})
                res = await s.call_tool("email_meme", {"name": "homework"})
                return names, sorted(props), json.loads(res.content[0].text)
    names, props, res = asyncio.run(mcpcheck())
    ok(names == ["email_meme"] and props == ["confirm", "name", "note", "to"], "tools/list: email_meme(name,to,note,confirm)")
    ok(res["status"] == "confirm_needed" and len(SENT) == 4, "tool call over MCP without confirm: read-back, no send")

    print("repo hygiene")
    pack = os.path.abspath(os.path.join(HERE, ".."))
    hits = []
    for root, _, files in os.walk(pack):
        if "results" in root.split(os.sep) or "__pycache__" in root:
            continue
        for f in files:
            t = open(os.path.join(root, f), errors="replace").read()
            for m in re.findall(r"[A-Za-z0-9._%+\-]+@[A-Za-z0-9\-]+\.[A-Za-z]{2,}", t):
                hits.append((f, m))
            if re.search(r"\b(am|sk)_[A-Za-z0-9]{16,}", t):
                hits.append((f, "key-like string"))
    ok(not hits, "no email addresses or key-like strings in the pack files" + (" %r" % hits if hits else ""))
    srv.shutdown()
    print("tests: %d passed, %d failed" % (passed, failed))
    print("EMAIL MEME TESTS ALL PASS" if not failed else "EMAIL MEME TESTS FAILED")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
