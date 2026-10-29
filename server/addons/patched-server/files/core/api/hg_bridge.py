"""headphone-girl bridge: lets Hermes Agent reach the StackChan's device tools.

Why: with Intent=nointent the xiaozhi server never hands device MCP tools to
the LLM, and Hermes's API server runs its own tool loop anyway. So Hermes
gets the robot's tools through a small MCP server (hermes/stackchan_mcp.py)
that calls these two HTTP endpoints on this server:

  POST /hg/api/device_call  {"tool": "self.led.light", "args": {...}}
  POST /hg/api/play_music   {"song": "name or empty for random", "dj": false}
  POST /hg/api/music/skip   {"direction": "next" | "prev"}   (v3.6.7-dj, real play history;
                            v3.6.7-radio: changes station while the radio is on or paused)
  POST /hg/api/radio        {"action": "play|next|prev|stop|list|status|resume", "station": "..."}
  GET  /hg/api/tools        allowlist + whether a robot is online

v3 phone music page (same token, passed as ?k=TOKEN because a phone browser
can't add a header):
  GET  /hg/music?k=TOKEN          list tracks, upload form, play buttons
  POST /hg/music/upload?k=TOKEN   multipart "file" (.mp3 only, 60 MB cap)
  POST /hg/music/play?k=TOKEN     form "song" (empty = random)
  POST /hg/music/skip?k=TOKEN     form "direction" next | prev (v3.6.7-dj)

meme-screen v1 (staged 2026-10-28):
  POST /hg/api/show_meme  {"png_b64": "...", "title": "..."}  (bearer token, LAN only, like device_call)
       -> keeps the PNG in memory under a random one-time ticket (90 s, 2 fetches), then sends a raw MCP
          tools/call self.screen.preview_image {"url": "<public>/hg/music?meme=<ticket>"} to the robot.
  GET  /hg/music?meme=<ticket>  serves that PNG once to the robot (image/png, no-store). It rides the
       /hg/music path because the stackchan tunnel only forwards exact paths (/hg/api/* is not public).
  self.screen.preview_image is user-only in the firmware, so the server never lists it and call_mcp_tool
  refuses it; this endpoint bypasses has_tool for that ONE tool name only. It is NOT in ALLOWED_TOOLS,
  so /hg/api/device_call still can't point the robot at an arbitrary URL. The URL is never logged.

small-preview (staged 2026-10-28): show_meme only accepts PNGs up to 160x120 and 64 KB. A 320x240
  preview (60 KB file, 307 KB decoded) ran firmware v3.6.7-dj6 out of memory and rebooted her. The
  firmware draws a 160-wide preview at scale 1.0, so it is the same size on screen as before.
  Hermes robot_show_meme makes the 160x120 copy in memory; saved meme files are unchanged.

Safety:
  * Bearer token required (config hg_bridge.token; placeholder until set).
  * Only localhost / private LAN callers.
  * Only allowlisted device tools; everything else is refused.
  * Face data and photos only ever move between the robot and this server.
"""

import html
import json
import os
import re
import uuid
import base64
import secrets
import struct
import time
import asyncio

from aiohttp import web

TAG = __name__

ALLOWED_TOOLS = {
    # face + lights
    "self.face.show", "self.led.light", "self.led.set_color", "self.led.off",
    "self.led.auto", "self.led.get_state",
    # head
    "self.servo.set_angles", "self.servo.center", "self.servo.look_around",
    "self.servo.get_angles", "self.motion.set_auto", "self.motion.get_auto",
    # camera (upstream tool; the device uploads the photo to vision_explain)
    "self.camera.take_photo",
    # v3.6.3 firmware: face following on/off (same as Settings > Head > Follow faces)
    "self.camera.follow_faces",
    # SD card
    "self.sd.status", "self.sd.list", "self.sd.read_text", "self.sd.write_text",
    "self.sd.save_photo",
    # people
    "self.profile.save", "self.profile.load", "self.profile.list",
    "self.face_cache.save", "self.face_cache.load", "self.face_cache.list",
    # dances
    "self.dance.record_start", "self.dance.record_stop", "self.dance.play",
    "self.dance.stop", "self.dance.list", "self.dance.preset", "self.dance.groove",
    # v3: music on the SD card, settings screen
    "self.music.sd", "self.settings.open",
    # v3.6.7-dj firmware: DJ screen on/off/status/track
    "self.dj.mode",
    # status
    "self.sensor.get_status", "self.get_device_status", "self.audio_speaker.set_volume",
}

_PLACEHOLDER = "CHANGE_ME_HG_BRIDGE_TOKEN"

# ---- meme-screen v1 -------------------------------------------------------------------------
SCREEN_PREVIEW_TOOL = "self.screen.preview_image"   # the ONLY user-only tool this file may call raw
MEME_MAX_BYTES = 64 * 1024              # small-preview: was 600 KB
MEME_MAX_W, MEME_MAX_H = 160, 120       # small-preview: was 640x480 (dj6 crashed on 320x240)
MEME_TTL_S = 90.0
MEME_FETCHES = 2
MEME_KEEP = 4
_PNG_SIG = b"\x89PNG\r\n\x1a\n"


def png_size(data):
    """(w, h) of a PNG, or None if it isn't one."""
    if not isinstance(data, (bytes, bytearray)) or len(data) < 33 or data[:8] != _PNG_SIG \
            or data[12:16] != b"IHDR":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return (w, h) if w > 0 and h > 0 else None


def public_base(config):
    """https://<host> the robot already uses (server.websocket), or hg_bridge.meme_base_url."""
    cfg = (config or {}).get("hg_bridge") or {}
    b = str(cfg.get("meme_base_url") or "").strip().rstrip("/")
    if b:
        return b
    ws = str(((config or {}).get("server") or {}).get("websocket") or "")
    m = re.match(r"^(wss?)://([^/]+)", ws)
    if not m:
        return ""
    return ("https://" if m.group(1) == "wss" else "http://") + m.group(2)


class HgBridge:
    def __init__(self, config, logger):
        self.config = config
        self.logger = logger
        cfg = (config or {}).get("hg_bridge") or {}
        self.token = str(cfg.get("token") or "")
        # Separate token for the phone music page (falls back to the bridge token)
        self.music_token = str(cfg.get("music_token") or "") or self.token
        self.enabled = bool(cfg.get("enabled", False)) and self.token not in ("", _PLACEHOLDER)
        self._memes = {}   # meme-screen v1: ticket -> [png bytes, expires_monotonic, fetches_left]

    # same LAN rule as the Fairy console
    @staticmethod
    def _is_lan(host):
        if not host:
            return False
        if host in ("::1", "localhost"):
            return True
        if host.startswith("::ffff:"):
            host = host[7:]
        if host.startswith(("127.", "10.", "192.168.")):
            return True
        if host.startswith("172."):
            try:
                return 16 <= int(host.split(".")[1]) <= 31
            except Exception:
                return False
        return False

    def _authorized(self, request):
        if not self.enabled:
            return False, "bridge disabled (set hg_bridge.enabled and a real token)"
        peer = request.transport.get_extra_info("peername")
        if not self._is_lan(peer[0] if peer else ""):
            return False, "LAN only"
        is_music = request.path.startswith("/hg/music")
        # Anything that came through the Cloudflare tunnel carries this header.
        # Only the music page is meant to be public; the bridge stays local.
        if request.headers.get("Cf-Connecting-IP") and not is_music:
            return False, "LAN only"
        import hmac as _hmac
        want = self.music_token if is_music else self.token
        given = request.headers.get("Authorization", "")
        given = given[7:] if given.startswith("Bearer ") else request.query.get("k", "") if is_music else ""
        auth_ok = bool(want) and _hmac.compare_digest(given, want)
        if not auth_ok:
            return False, "bad token"
        return True, ""

    @staticmethod
    def _conn():
        try:
            from core.api import device_registry
            return device_registry.get()
        except Exception:
            return None

    async def handle_tools(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.json_response({"ok": False, "error": why}, status=403)
        conn = self._conn()
        return web.json_response({"ok": True, "online": conn is not None,
                                  "tools": sorted(ALLOWED_TOOLS),
                                  "screen": ["show_meme"]})   # meme-screen v1

    async def handle_device_call(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.json_response({"ok": False, "error": why}, status=403)
        try:
            body = await request.json()
        except Exception:
            return web.json_response({"ok": False, "error": "bad json"}, status=400)
        tool = str(body.get("tool", ""))
        args = body.get("args") or {}
        if tool not in ALLOWED_TOOLS:
            return web.json_response({"ok": False, "error": "tool not allowed"}, status=403)
        if not isinstance(args, dict):
            return web.json_response({"ok": False, "error": "args must be an object"}, status=400)
        conn = self._conn()
        if conn is None or getattr(conn, "mcp_client", None) is None:
            return web.json_response({"ok": False, "error": "robot offline"})
        from core.providers.tools.device_mcp.mcp_handler import call_mcp_tool
        # the device registers tools with dots; the server exposes them with _
        wire_name = tool.replace(".", "_")
        if tool == "self.dj.mode" and args.get("action") in ("on", "off"):
            conn.hg_dj = args.get("action") == "on"   # v3.6.7-dj: DJ intros follow the robot
        try:
            res = await call_mcp_tool(conn, conn.mcp_client, wire_name, json.dumps(args),
                                      timeout=45)
            return web.json_response({"ok": True, "tool": tool, "result": str(res)[:4000]})
        except Exception as e:
            return web.json_response({"ok": False, "error": "device call failed: %r" % e})

    async def handle_play_music(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.json_response({"ok": False, "error": why}, status=403)
        try:
            body = await request.json()
        except Exception:
            body = {}
        conn = self._conn()
        if conn is None or getattr(conn, "tts", None) is None:
            return web.json_response({"ok": False, "error": "robot offline"})
        from plugins_func.functions import play_music as pm
        from core.providers.tts.dto.dto import TTSMessageDTO, SentenceType, ContentType
        cache = pm.initialize_music_handler(conn)
        song = str(body.get("song", "")).strip()
        if body.get("dj") is True:
            pm.hg_set_dj(conn, True)   # v3.6.7-dj
        match = pm._find_best_match(song, cache.get("music_files", [])) if song else None
        conn.sentence_id = uuid.uuid4().hex
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.FIRST,
                                                  content_type=ContentType.ACTION))
        await pm.play_local_music(conn, specific_file=match)
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.LAST,
                                                  content_type=ContentType.ACTION))
        return web.json_response({"ok": True, "song": match or "random",
                                  "library": len(cache.get("music_files", []))})

    async def _skip(self, conn, direction):
        """v3.6.7-dj: stop what is playing, then play next / previous from the history."""
        from plugins_func.functions import play_music as pm
        from core.providers.tts.dto.dto import TTSMessageDTO, SentenceType, ContentType
        try:   # v3.6.7-radio: while the radio is on or paused, next / previous change station
            from core.handle import radioHandler as rh
            if rh._radio(conn) is not None:
                res = await rh.radio_command(conn, direction, heard="")
                return ("radio: %s" % res["station"]) if res.get("ok") else None
        except Exception as e:
            self.logger.bind(tag=TAG).warning("skip: radio check failed: %r" % e)
        if getattr(conn, "hg_setlist", None) is not None:
            # a DJ set list is on: next / previous step through its songs in order
            from core.handle.setlistHandler import hg_try_setlist
            await hg_try_setlist(conn, "next song" if direction == "next" else "previous song")
            st = getattr(conn, "hg_setlist", None)
            return ("set list: %s" % st.song()["track"]) if st is not None else "set list ended"
        if getattr(conn, "client_is_speaking", False):
            try:
                from core.handle.abortHandle import handleAbortMessage
                await handleAbortMessage(conn)
            except Exception as e:
                self.logger.bind(tag=TAG).warning("skip: abort failed: %r" % e)
            import asyncio
            await asyncio.sleep(0.3)
            conn.client_abort = False
        conn.sentence_id = uuid.uuid4().hex
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.FIRST,
                                                  content_type=ContentType.ACTION))
        pick = await pm.hg_skip(conn, direction)
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.LAST,
                                                  content_type=ContentType.ACTION))
        return pick

    async def handle_music_skip_api(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.json_response({"ok": False, "error": why}, status=403)
        try:
            body = await request.json()
        except Exception:
            body = {}
        direction = str(body.get("direction", "next")).strip().lower()
        if direction in ("previous", "back", "prior", "last"):
            direction = "prev"
        if direction not in ("next", "prev"):
            return web.json_response({"ok": False, "error": "direction must be next or prev"}, status=400)
        conn = self._conn()
        if conn is None or getattr(conn, "tts", None) is None:
            return web.json_response({"ok": False, "error": "robot offline"})
        pick = await self._skip(conn, direction)
        if pick is None:
            return web.json_response({"ok": False, "error": "no songs in the music folder"})
        return web.json_response({"ok": True, "direction": direction, "song": pick})

    async def handle_music_skip(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.Response(text=why, status=403)
        form = await request.post()
        direction = "prev" if str(form.get("direction", "")) == "prev" else "next"
        conn = self._conn()
        if conn is not None and getattr(conn, "tts", None) is not None:
            await self._skip(conn, direction)
        raise web.HTTPSeeOther("/hg/music?k=" + request.query.get("k", ""))

    # ── v3: phone music page ────────────────────────────────────────────────
    MAX_UPLOAD = 60 * 1024 * 1024

    def _music_dir(self):
        pm = ((self.config or {}).get("plugins") or {}).get("play_music") or {}
        d = pm.get("music_dir", "./music")
        os.makedirs(d, exist_ok=True)
        return d

    @staticmethod
    def _safe_name(name):
        base = os.path.basename(name or "")
        stem, ext = os.path.splitext(base)
        stem = re.sub(r"[^A-Za-z0-9 _.()'-]", "_", stem).strip(" .")[:80]
        if ext.lower() != ".mp3" or not stem:
            return None
        return stem + ".mp3"

    async def handle_music_page(self, request):
        if "meme" in request.query:   # meme-screen v1: the robot fetching a one-time meme ticket
            return self._serve_meme(request)
        ok, why = self._authorized(request)
        if not ok:
            return web.Response(text=why, status=403)
        k = html.escape(request.query.get("k", ""), quote=True)
        d = self._music_dir()
        tracks = sorted(f for f in os.listdir(d) if f.lower().endswith(".mp3"))
        rows = "".join(
            '<li><form method="post" action="/hg/music/play?k=%s">'
            '<input type="hidden" name="song" value="%s">'
            '<button>&#9654;</button> %s</form></li>'
            % (k, html.escape(t, quote=True), html.escape(t)) for t in tracks)
        online = self._conn() is not None
        page = """<!doctype html><meta name=viewport content="width=device-width,initial-scale=1">
<title>Robot music</title>
<style>body{font:18px system-ui;margin:16px;max-width:560px}button{font-size:18px;padding:6px 14px}
li{list-style:none;margin:8px 0}ul{padding:0}.s{color:#777}</style>
<h2>Robot music</h2><p class=s>Robot %s. Files live in the server music folder.</p>
<form method=post enctype=multipart/form-data action="/hg/music/upload?k=%s">
<input type=file name=file accept=".mp3,audio/mpeg"> <button>Upload</button></form>
<form method=post action="/hg/music/play?k=%s"><input type=hidden name=song value="">
<p><button>Play something random</button></p></form>
<p><form style="display:inline" method=post action="/hg/music/skip?k=%s"><input type=hidden name=direction value=prev><button>&#9198; Previous</button></form>
<form style="display:inline" method=post action="/hg/music/skip?k=%s"><input type=hidden name=direction value=next><button>Next &#9197;</button></form></p>
<ul>%s</ul>""" % (
            "online" if online else "offline", k, k, k, k, rows or "<li class=s>No MP3s yet.</li>")
        return web.Response(text=page, content_type="text/html")

    async def handle_music_upload(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.Response(text=why, status=403)
        reader = await request.multipart()
        field = await reader.next()
        while field is not None and field.name != "file":
            field = await reader.next()
        if field is None:
            return web.Response(text="no file", status=400)
        name = self._safe_name(field.filename)
        if name is None:
            return web.Response(text="only .mp3 files", status=400)
        path = os.path.join(self._music_dir(), name)
        size = 0
        tmp = path + ".part"
        with open(tmp, "wb") as f:
            while True:
                chunk = await field.read_chunk(256 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > self.MAX_UPLOAD:
                    f.close()
                    os.remove(tmp)
                    return web.Response(text="file too big (60 MB max)", status=413)
                f.write(chunk)
        os.replace(tmp, path)
        self.logger.bind(tag=TAG).info("music upload %s (%d bytes)" % (name, size))
        raise web.HTTPSeeOther("/hg/music?k=" + request.query.get("k", ""))

    async def handle_music_play(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.Response(text=why, status=403)
        form = await request.post()
        conn = self._conn()
        if conn is not None and getattr(conn, "tts", None) is not None:
            await self._play(conn, str(form.get("song", "")).strip())
        raise web.HTTPSeeOther("/hg/music?k=" + request.query.get("k", ""))

    async def _play(self, conn, song):
        from plugins_func.functions import play_music as pm
        from core.providers.tts.dto.dto import TTSMessageDTO, SentenceType, ContentType
        cache = pm.initialize_music_handler(conn)
        files = cache.get("music_files", [])
        match = song if song in files else (pm._find_best_match(song, files) if song else None)
        conn.sentence_id = uuid.uuid4().hex
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.FIRST,
                                                  content_type=ContentType.ACTION))
        await pm.play_local_music(conn, specific_file=match)
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.LAST,
                                                  content_type=ContentType.ACTION))
        return match, len(files)

    async def handle_radio_api(self, request):
        """v3.6.7-radio: {"action": "play|next|prev|stop|list|status|resume", "station": "name/alias/id"}"""
        ok, why = self._authorized(request)
        if not ok:
            return web.json_response({"ok": False, "error": why}, status=403)
        try:
            body = await request.json()
        except Exception:
            body = {}
        if not isinstance(body, dict):
            body = {}
        action = str(body.get("action", "play")).strip().lower()
        action = {"previous": "prev", "back": "prev", "on": "play", "start": "play", "off": "stop",
                  "skip": "next", "continue": "resume", "stations": "list"}.get(action, action)
        if action not in ("play", "next", "prev", "stop", "list", "status", "resume"):
            return web.json_response({"ok": False, "error": "action must be play, next, prev, stop, list, status or resume"},
                                     status=400)
        station = str(body.get("station", "") or "").strip()[:80]
        from core.handle import radioHandler as rh
        conn = self._conn()
        if action == "list" and conn is None:
            try:
                stations, _ = rh.load_stations()
                return web.json_response({"ok": True, "online": False, "stations": [
                    {"id": s["id"], "name": s["name"], "genre": s["genre"]} for s in stations]})
            except Exception as e:
                return web.json_response({"ok": False, "error": "stations file unusable: %s" % e})
        if conn is None or getattr(conn, "tts", None) is None:
            return web.json_response({"ok": False, "error": "robot offline"})
        if action in ("play", "next", "prev", "resume") and getattr(conn, "client_is_speaking", False) \
                and rh._radio(conn) is None:
            try:   # she is talking or playing a song: stop that first, like music skip does
                from core.handle.abortHandle import handleAbortMessage
                await handleAbortMessage(conn)
            except Exception as e:
                self.logger.bind(tag=TAG).warning("radio: abort failed: %r" % e)
            import asyncio
            await asyncio.sleep(0.3)
            conn.client_abort = False
        res = await rh.radio_command(conn, action, station, heard="")
        return web.json_response(res)

    # ---- meme-screen v1 ---------------------------------------------------------------------
    def _gc_memes(self):
        now = time.monotonic()
        for t in [t for t, v in self._memes.items() if v[1] < now or v[2] <= 0]:
            self._memes.pop(t, None)
        while len(self._memes) > MEME_KEEP:
            self._memes.pop(next(iter(self._memes)), None)

    def _serve_meme(self, request):
        peer = request.transport.get_extra_info("peername") if request.transport else None
        if not self.enabled or not self._is_lan(peer[0] if peer else ""):
            return web.Response(text="not found", status=404)
        self._gc_memes()
        t = str(request.query.get("meme", ""))
        v = self._memes.get(t) if 20 <= len(t) <= 64 else None
        if v is None:
            return web.Response(text="not found", status=404)
        v[2] -= 1
        return web.Response(body=v[0], content_type="image/png",
                            headers={"Cache-Control": "no-store", "X-Robots-Tag": "noindex"})

    async def _raw_screen_call(self, conn, url, timeout=30.0):
        """tools/call for SCREEN_PREVIEW_TOOL only, without has_tool and without logging the URL."""
        mc = conn.mcp_client
        if not await mc.is_ready():
            raise RuntimeError("robot MCP not ready")
        if not (getattr(conn, "features", None) or {}).get("mcp"):
            raise RuntimeError("robot has no MCP")
        call_id = await mc.get_next_id()
        fut = asyncio.get_running_loop().create_future()
        await mc.register_call_result_future(call_id, fut)
        msg = json.dumps({"type": "mcp", "payload": {
            "jsonrpc": "2.0", "id": call_id, "method": "tools/call",
            "params": {"name": SCREEN_PREVIEW_TOOL, "arguments": {"url": url}}}})
        try:
            await conn.websocket.send(msg)
            raw = await asyncio.wait_for(fut, timeout=timeout)
        except Exception:
            await mc.cleanup_call_result(call_id)
            raise
        if isinstance(raw, dict) and raw.get("isError") is True:
            raise RuntimeError("robot said: %s" % str(raw.get("content") or raw.get("error"))[:200])
        return raw

    async def handle_show_meme(self, request):
        ok, why = self._authorized(request)
        if not ok:
            return web.json_response({"ok": False, "error": why}, status=403)
        try:
            body = await request.json()
            data = base64.b64decode(str(body.get("png_b64", "")), validate=True)
        except Exception:
            return web.json_response({"ok": False, "error": "send JSON with png_b64"}, status=400)
        if len(data) > MEME_MAX_BYTES:
            return web.json_response({"ok": False, "error": "png too big"}, status=413)
        size = png_size(data)
        if size is None:
            return web.json_response({"ok": False, "error": "not a PNG (the robot decodes PNG only)"},
                                     status=400)
        if size[0] > MEME_MAX_W or size[1] > MEME_MAX_H:
            return web.json_response({"ok": False, "error": "png larger than %dx%d (send the small preview)"
                                      % (MEME_MAX_W, MEME_MAX_H)}, status=400)
        base = public_base(self.config)
        if not base:
            return web.json_response({"ok": False, "error": "no public base url (server.websocket)"})
        conn = self._conn()
        if conn is None or getattr(conn, "mcp_client", None) is None:
            return web.json_response({"ok": False, "error": "robot offline"})
        self._gc_memes()
        ticket = secrets.token_urlsafe(24)
        self._memes[ticket] = [bytes(data), time.monotonic() + MEME_TTL_S, MEME_FETCHES]
        title = re.sub(r"[^A-Za-z0-9 _.-]", "", str(body.get("title", "")))[:60]
        try:
            await self._raw_screen_call(conn, "%s/hg/music?meme=%s" % (base, ticket))
        except Exception as e:
            self._memes.pop(ticket, None)
            self.logger.bind(tag=TAG).warning("show_meme failed: %s" % type(e).__name__)
            return web.json_response({"ok": False, "error": "robot could not show it: %s"
                                      % str(e).replace(ticket, "***")[:200]})
        self.logger.bind(tag=TAG).info("show_meme %r %dx%d %d bytes" % (title, size[0], size[1], len(data)))
        return web.json_response({"ok": True, "shown": title or "meme", "width": size[0],
                                  "height": size[1], "seconds": 5})

    def register(self, app):
        app.add_routes([
            web.get("/hg/api/tools", self.handle_tools),
            web.post("/hg/api/device_call", self.handle_device_call),
            web.post("/hg/api/play_music", self.handle_play_music),
            web.get("/hg/music", self.handle_music_page),
            web.post("/hg/music/upload", self.handle_music_upload),
            web.post("/hg/music/play", self.handle_music_play),
            web.post("/hg/api/music/skip", self.handle_music_skip_api),   # v3.6.7-dj
            web.post("/hg/music/skip", self.handle_music_skip),           # v3.6.7-dj
            web.post("/hg/api/radio", self.handle_radio_api),             # v3.6.7-radio
            web.post("/hg/api/show_meme", self.handle_show_meme),         # meme-screen v1
        ])
        self.logger.bind(tag=TAG).info(
            "hg bridge %s" % ("enabled" if self.enabled else "disabled (no token)"))
