#!/usr/bin/env python3
"""nousgirl_memes: tiny local MCP server (stdio) with ONE tool, save_meme(url, title).

Save helper for nousgirl_localmeme_mcp.py (it imports save_bytes from here). Run on its own, it is also a
tiny MCP server that downloads a finished meme image from an https URL, keeps the original in ~/NousGirl-memes/ with a timestamp, and writes two 320x240 copies
for the CoreS3 screen (letterboxed on black, nothing cropped):
    <stamp>-<slug>.<ext>          the original
    <stamp>-<slug>-320x240.jpg    small JPEG (to share / look at)
    <stamp>-<slug>-320x240.png    same picture as PNG: the v3.6.7 firmware decodes PNG only (LVGL LodePNG on,
                                  TJPGD off), so this is the file a future screen push would use.
It never uploads anything, never calls the robot, needs no keys.

Runtime (same as stackchan_mcp_addonly.py, plus Pillow):
    uv run --no-project --quiet --python 3.12 --with 'mcp>=1.2,<2' --with httpx --with pillow \
        python nousgirl_memes_mcp.py            # MCP stdio server (Hermes starts this)
    ... python nousgirl_memes_mcp.py --save URL [--title T]   # manual test, prints JSON
    ... python nousgirl_memes_mcp.py --selftest               # offline, no network
Env: NG_MEMES_DIR (default ~/NousGirl-memes), NG_MEMES_ALLOW_HOSTS (comma list of host suffixes;
default: any https host), NG_MEMES_MAX_BYTES (default 10 MB).
"""
import io
import json
import os
import re
import sys
import time
from urllib.parse import urlparse

SCREEN = (320, 240)
MAX_BYTES = int(os.environ.get("NG_MEMES_MAX_BYTES", str(10 * 1024 * 1024)))
OUT_DIR = os.path.expanduser(os.environ.get("NG_MEMES_DIR", "~/NousGirl-memes"))
ALLOW = [h.strip().lower() for h in os.environ.get("NG_MEMES_ALLOW_HOSTS", "").split(",") if h.strip()]
EXT = {"JPEG": "jpg", "PNG": "png", "WEBP": "webp", "GIF": "gif"}


def _slug(text: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", (text or "").lower()).strip("-")
    return (s[:40].rstrip("-")) or "meme"


def check_url(url: str) -> str:
    u = urlparse(url or "")
    if u.scheme != "https" or not u.hostname:
        raise ValueError("only https image URLs are accepted")
    host = u.hostname.lower()
    if host in ("localhost",) or host.startswith("127.") or host.startswith("10.") or host.startswith("192.168.") \
            or host.endswith(".local") or re.match(r"^172\.(1[6-9]|2\d|3[01])\.", host):
        raise ValueError("local / private hosts are not accepted")
    if ALLOW and not any(host == a or host.endswith("." + a) for a in ALLOW):
        raise ValueError("host not in NG_MEMES_ALLOW_HOSTS")
    return url


def fit_320x240(img):
    from PIL import Image
    img = img.convert("RGB")
    img.thumbnail(SCREEN, Image.LANCZOS)          # keep aspect, never upscale past the box
    canvas = Image.new("RGB", SCREEN, (0, 0, 0))
    canvas.paste(img, ((SCREEN[0] - img.width) // 2, (SCREEN[1] - img.height) // 2))
    return canvas


def save_bytes(data: bytes, title: str = "", out_dir: str = OUT_DIR, now: float = None) -> dict:
    from PIL import Image
    if len(data) > MAX_BYTES:
        raise ValueError("image too large")
    img = Image.open(io.BytesIO(data))
    img.verify()                                   # rejects non-images / truncated files
    img = Image.open(io.BytesIO(data))
    fmt = (img.format or "").upper()
    if fmt not in EXT:
        raise ValueError("unsupported image type %r" % fmt)
    if getattr(img, "is_animated", False):
        img.seek(0)                                # first frame for the screen copies
    os.makedirs(out_dir, mode=0o700, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime(now if now is not None else time.time()))
    base = os.path.join(out_dir, "%s-%s" % (stamp, _slug(title)))
    n = 1
    while os.path.exists(base + "." + EXT[fmt]):
        n += 1; base = os.path.join(out_dir, "%s-%s-%d" % (stamp, _slug(title), n))
    orig = base + "." + EXT[fmt]
    with open(orig, "wb") as f:
        f.write(data)
    small = fit_320x240(img)
    jpg, png = base + "-320x240.jpg", base + "-320x240.png"
    small.save(jpg, "JPEG", quality=85, optimize=True, progressive=False)  # baseline JPEG
    small.save(png, "PNG", optimize=True)
    return {"ok": True, "original": orig, "screen_jpeg": jpg, "screen_png": png,
            "size": [img.width, img.height], "format": fmt,
            "on_robot_screen": False,
            "note": "Saved on your computer. Showing it on her screen is not wired up yet."}


def download(url: str) -> bytes:
    import httpx
    check_url(url)
    with httpx.Client(timeout=20.0, follow_redirects=True, max_redirects=3) as c:
        with c.stream("GET", url, headers={"User-Agent": "NousGirl-memes/1"}) as r:
            if str(r.url).split(":", 1)[0] != "https":
                raise ValueError("redirected away from https")
            check_url(str(r.url))
            r.raise_for_status()
            ctype = r.headers.get("content-type", "")
            if not ctype.startswith("image/"):
                raise ValueError("not an image (content-type %s)" % ctype.split(";")[0])
            buf = bytearray()
            for chunk in r.iter_bytes():
                buf += chunk
                if len(buf) > MAX_BYTES:
                    raise ValueError("image too large")
    return bytes(buf)


def save_meme_impl(url: str, title: str = "") -> dict:
    try:
        return save_bytes(download(url), title)
    except Exception as e:  # short, no URLs with query strings echoed back
        return {"ok": False, "error": str(e)[:200]}


def selftest() -> int:
    import tempfile
    from PIL import Image
    fails = 0
    def ok(cond, what):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + what); fails += 0 if cond else 1
    with tempfile.TemporaryDirectory() as d:
        for size, fmt in (((1200, 1200), "PNG"), ((800, 450), "JPEG"), ((300, 900), "WEBP")):
            b = io.BytesIO(); Image.new("RGB", size, (200, 30, 90)).save(b, fmt)
            r = save_bytes(b.getvalue(), "Test: my homework meme!", out_dir=d)
            s1 = Image.open(r["screen_jpeg"]); s2 = Image.open(r["screen_png"])
            ok(s1.size == SCREEN and s1.format == "JPEG", "%s %s -> 320x240 JPEG" % (fmt, size))
            ok(s2.size == SCREEN and s2.format == "PNG", "%s %s -> 320x240 PNG" % (fmt, size))
            ok(os.path.basename(r["original"]).split("-", 2)[2].startswith("test-my-homework-meme"), "slug + timestamp name")
        try:
            save_bytes(b"not an image", "x", out_dir=d); ok(False, "rejects non-image")
        except Exception:
            ok(True, "rejects non-image")
        for bad in ("http://www.memelord.com/x.png", "https://127.0.0.1/x.png", "https://192.168.1.10/x.png",
                    "file:///etc/passwd", "https://foo.local/x.png"):
            try:
                check_url(bad); ok(False, "rejects " + bad)
            except ValueError:
                ok(True, "rejects " + bad)
        ok(check_url("https://www.memelord.com/x.png") == "https://www.memelord.com/x.png", "accepts https host")
        ok(oct(os.stat(d).st_mode)[-3:] in ("700", "755") , "out dir exists")
    print("ALL PASS" if not fails else "%d FAILED" % fails)
    return 1 if fails else 0


def main():
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    if "--save" in sys.argv:
        i = sys.argv.index("--save"); url = sys.argv[i + 1]
        title = sys.argv[sys.argv.index("--title") + 1] if "--title" in sys.argv else ""
        print(json.dumps(save_meme_impl(url, title), indent=1)); return
    from mcp.server.fastmcp import FastMCP
    mcp = FastMCP("nousgirl_memes")

    @mcp.tool()
    def save_meme(url: str, title: str = "") -> str:
        """Save a finished meme image (the https image URL a Memelord tool returned) on your computer in
        ~/NousGirl-memes/, plus a 320x240 copy for the robot screen. Call it once after every meme you
        make. Returns JSON with the saved file paths. It does not show the meme on the robot screen."""
        return json.dumps(save_meme_impl(url, title))

    mcp.run()


if __name__ == "__main__":
    main()
