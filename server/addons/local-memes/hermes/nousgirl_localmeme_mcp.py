#!/usr/bin/env python3
"""nousgirl_localmeme: LOCAL meme maker (stdio MCP) for Nous-Girl. No network, no keys, no AI image model.

Two tools:
  list_templates(query="")                         -> the classic templates on this computer, with box counts + hints
  make_meme(template, top_text="", bottom_text="", lines=None, title="")
      renders the caption(s) on a blank template with Pillow (Impact-style white text, black outline,
      auto-wrap + shrink to fit each template's text boxes) and saves it through the SAME three-format
      pipeline as save_meme (nousgirl_memes_mcp.save_bytes):
          ~/NousGirl-memes/<stamp>-<slug>.webp  +  -320x240.jpg  +  -320x240.png
      so robot_show_meme name "latest" finds it. Returns paths + ms timings.

Run (Hermes starts it; same runtime style as the other helpers):
  uv run --no-project --quiet --python 3.12 --with 'mcp>=1.2,<2' --with pillow python nousgirl_localmeme_mcp.py
  ... --selftest                      offline checks (renders into a temp dir)
  ... --make TEMPLATE "TOP" "BOTTOM" [--lines a|b|c] [--out DIR]   manual test, prints JSON
Env: NG_TEMPLATES_DIR (default ~/NousGirl-meme-templates), NG_TEMPLATES_JSON (default: nousgirl_localmeme_templates.json
or templates.json next to this file, then ../templates.json), NG_MEMES_DIR (default ~/NousGirl-memes),
NG_MEME_FONT (a .ttf path; default macOS Impact, else Anton in NG_TEMPLATES_DIR/fonts).
"""
import difflib, io, json, os, re, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
STAGED = os.path.join(HERE, "..")
TPL_DIR = os.path.expanduser(os.environ.get("NG_TEMPLATES_DIR", "~/NousGirl-meme-templates"))
OUT_DIR = os.path.expanduser(os.environ.get("NG_MEMES_DIR", "~/NousGirl-memes"))
FONTS = [os.environ.get("NG_MEME_FONT", ""), "/System/Library/Fonts/Supplemental/Impact.ttf",
         os.path.join(TPL_DIR, "fonts", "Anton-Regular.ttf"), "/Library/Fonts/Impact.ttf"]
MAX_CHARS = 120          # per box
MAX_TOTAL = 400

# Save pipeline: an optional nousgirl_memes_mcp.py next to this file wins, otherwise meme_save.py
# (shipped next to this file).
for _p in (HERE,):
    if os.path.isfile(os.path.join(_p, "nousgirl_memes_mcp.py")):  # optional Memelord helper, not shipped
        sys.path.insert(0, _p); break
try:
    import nousgirl_memes_mcp as _save
    SAVE_SRC = "nousgirl_memes_mcp.py (live helper)"
except Exception:
    for _p in (HERE, STAGED):
        if os.path.isfile(os.path.join(_p, "meme_save.py")):
            sys.path.insert(0, _p); break
    import meme_save as _save
    SAVE_SRC = "meme_save.py"

# Backup kid-safety net (the SOUL rules are the main guard). Whole-word, case-insensitive.
# NG_MEME_BLOCKED_NAMES: comma list of real names (your family, your kids) that must never go on a meme.
def _block_re(names):
    return re.compile(r"\b(" + "|".join(BASE_BLOCK + [re.escape(n.strip()) for n in names.split(",") if n.strip()]) + r")\b", re.I)


BASE_BLOCK = ([
    r"f+u+c+k\w*", r"sh[i1]t\w*", r"bitch\w*", r"ass(hole)?s?", r"dick\w*", r"cock\w*", r"puss(y|ies)", r"cunt\w*",
    r"bastard\w*", r"damn", r"crap", r"piss\w*", r"wtf", r"stfu", r"sex\w*", r"porn\w*", r"nude\w*", r"naked",
    r"boob\w*", r"kill(s|ed|ing)?", r"murder\w*", r"suicide", r"gun\w*", r"shoot\w*", r"blood\w*", r"die[ds]",
    r"drugs?", r"weed", r"cocaine", r"beer", r"vodka", r"drunk", r"high af", r"retard\w*", r"stupid", r"idiot\w*",
    r"ugly", r"nazi\w*", r"hitler", r"trump", r"biden", r"obama"])
BLOCK = _block_re(os.environ.get("NG_MEME_BLOCKED_NAMES", ""))

_cache = {"tpl": None, "img": {}, "font": {}}


def font_path():
    for p in FONTS:
        if p and os.path.isfile(p):
            return p
    raise RuntimeError("no meme font found (Impact or Anton)")


def font(size):
    from PIL import ImageFont
    f = _cache["font"].get(size)
    if f is None:
        f = _cache["font"][size] = ImageFont.truetype(font_path(), size)
    return f


def templates():
    if _cache["tpl"] is None:
        for p in (os.environ.get("NG_TEMPLATES_JSON", ""), os.path.join(HERE, "nousgirl_localmeme_templates.json"),
                  os.path.join(HERE, "templates.json"),
                  os.path.join(STAGED, "templates.json")):
            if p and os.path.isfile(p):
                _cache["tpl"] = {t["id"]: t for t in json.load(open(p))["templates"]
                                 if os.path.isfile(os.path.join(TPL_DIR, t["file"]))}
                break
        else:
            raise RuntimeError("templates.json not found")
    return _cache["tpl"]


def _norm(s):
    return re.sub(r"[^a-z0-9]+", " ", (s or "").lower()).strip()


def resolve(name):
    T = templates(); n = _norm(name)
    if not n:
        return None
    keys = {}
    for t in T.values():
        for k in [t["id"], t["name"]] + t.get("aliases", []):
            keys[_norm(k)] = t["id"]
    if n in keys:
        return T[keys[n]]
    for k, tid in keys.items():            # "drake meme" -> drake
        if n in k or (len(k) > 3 and k in n):
            return T[tid]
    m = difflib.get_close_matches(n, list(keys), n=1, cutoff=0.6)
    return T[keys[m[0]]] if m else None


def list_templates_impl(query=""):
    T = templates(); q = _norm(query)
    rows = []
    for t in T.values():
        hay = _norm(" ".join([t["id"], t["name"]] + t.get("aliases", []) + [t.get("hint", "")]))
        if q and not all(w in hay for w in q.split()):
            continue
        rows.append({"template": t["id"], "name": t["name"], "boxes": len(t["boxes"]), "how": t.get("hint", "")})
    return {"ok": True, "count": len(rows), "templates": rows[:60],
            "note": "Pass 'template' to make_meme. 2-box templates: top_text/bottom_text. More boxes: lines=[...] in box order."}


def _wrap(draw, text, f, maxw):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if draw.textlength(t, font=f) <= maxw or not cur:
            cur = t
        else:
            lines.append(cur); cur = w
    if cur:
        lines.append(cur)
    return lines


def _fit(draw, text, bw, bh, img_h):
    """largest font size whose wrapped lines fit the box (binary search)."""
    lo, hi = 10, max(12, min(int(bh * 0.9), int(img_h * 0.16)))
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        f = font(mid); lines = _wrap(draw, text, f, bw)
        lh = int(mid * 1.08); h = lh * len(lines)
        w = max((draw.textlength(l, font=f) for l in lines), default=0)
        if h <= bh and w <= bw:
            best = (mid, lines, lh); lo = mid + 1
        else:
            hi = mid - 1
    if best is None:                              # still too long at 10 px: hard-wrap at 10 px
        f = font(10); best = (10, _wrap(draw, text, f, bw), 11)
    return best


def render(t, texts):
    from PIL import Image, ImageDraw, ImageColor
    base = _cache["img"].get(t["id"])
    if base is None:
        base = _cache["img"][t["id"]] = Image.open(os.path.join(TPL_DIR, t["file"])).convert("RGB")
    img = base.copy(); W, H = img.size; d = ImageDraw.Draw(img)
    fits = []
    colors = t.get("colors") or ["white"]
    for i, (box, text) in enumerate(zip(t["boxes"], texts)):
        text = " ".join((text or "").split())
        if not text:
            continue
        styles = t.get("styles") or []
        st = styles[i] if i < len(styles) else ("upper" if t.get("upper", True) else "none")
        if st == "mock":       # sPoNgEbOb case
            k = 0; out = []
            for ch in text.lower():
                if ch.isalpha():
                    out.append(ch.upper() if k % 2 else ch); k += 1
                else:
                    out.append(ch)
            text = "".join(out)
        elif st == "lower":
            text = text.lower()
        elif st != "none":     # memegen "default" / "upper" = classic all caps
            text = text.upper()
        pad = max(4, int(W * 0.015))
        x0 = max(0, int(box[0] * W)) + pad; y0 = max(0, int(box[1] * H)) + pad // 2
        bw = max(20, min(W, int((box[0] + box[2]) * W)) - pad - x0); bh = max(14, min(H, int((box[1] + box[3]) * H)) - pad // 2 - y0)
        size, lines, lh = _fit(d, text, bw, bh, H)
        fill = colors[i] if i < len(colors) else colors[-1]
        try:
            rgb = ImageColor.getrgb(fill)
        except ValueError:
            rgb = (255, 255, 255)
        dark = sum(rgb[:3]) < 200
        stroke = (255, 255, 255) if dark else (0, 0, 0)
        sw = max(1, size // (18 if dark else 12))
        total = lh * len(lines)
        y = y0 + (bh - total) // 2
        for l in lines:
            lw = d.textlength(l, font=font(size))
            d.text((x0 + (bw - lw) / 2, y), l, font=font(size), fill=rgb, stroke_width=sw, stroke_fill=stroke)
            y += lh
        fits.append({"box": i + 1, "font_px": size, "lines": len(lines),
                     "fits": all(d.textlength(l, font=font(size)) <= bw for l in lines) and total <= bh + 2})
    return img, fits


def make_meme_impl(template, top_text="", bottom_text="", lines=None, title="", out_dir=None):
    t0 = time.perf_counter()
    try:
        t = resolve(template)
        if t is None:
            sugg = [r["template"] for r in list_templates_impl(template)["templates"][:5]]
            return {"ok": False, "error": "unknown template %r" % (template or "")[:60],
                    "try": sugg or ["drake", "db", "ds", "gb", "fine", "success"]}
        if isinstance(lines, str):
            lines = [s for s in re.split(r"\s*\|\s*|\n", lines)]
        texts = [str(x) for x in lines] if lines else [top_text or "", bottom_text or ""]
        texts = [" ".join(s.split())[:MAX_CHARS] for s in texts][: len(t["boxes"])]
        if not any(texts):
            return {"ok": False, "error": "no caption text"}
        if sum(len(s) for s in texts) > MAX_TOTAL:
            return {"ok": False, "error": "captions too long; keep them short and punchy"}
        bad = sorted({m.group(0).lower() for s in texts + [title or ""] for m in BLOCK.finditer(s)})
        if bad:
            return {"ok": False, "error": "caption not kid-safe or names a real person: %s. Rewrite it clean." % ", ".join(bad),
                    "blocked": True}
        t1 = time.perf_counter()
        img, fits = render(t, texts)
        t2 = time.perf_counter()
        buf = io.BytesIO(); img.save(buf, "WEBP", quality=85, method=2)      # original kept as WebP, like Memelord's
        t3 = time.perf_counter()
        r = _save.save_bytes(buf.getvalue(), title or (t["id"] + " " + " ".join(texts))[:60],
                             **({"out_dir": out_dir} if out_dir else {}))
        t4 = time.perf_counter()
        r.update({"template": t["id"], "template_name": t["name"], "boxes_used": sum(1 for s in texts if s),
                  "boxes": len(t["boxes"]), "text_fit": fits, "all_text_fits": all(f["fits"] for f in fits),
                  "ms": {"lookup": round((t1 - t0) * 1000, 1), "render": round((t2 - t1) * 1000, 1),
                         "encode_webp": round((t3 - t2) * 1000, 1), "save_3_formats": round((t4 - t3) * 1000, 1),
                         "total": round((t4 - t0) * 1000, 1)},
                  "on_robot_screen": False,
                  "note": "Saved on your computer in NousGirl-memes. To show it, call robot_show_meme name 'latest'."})
        return r
    except Exception as e:
        return {"ok": False, "error": str(e)[:200]}


def warm():
    """load templates, font, Pillow's WebP/PNG/JPEG codecs once at server start (first call was ~1 s cold)."""
    from PIL import Image
    try:
        templates(); font(40); render(templates()["drake"], ["warm", "up"])
        im = Image.new("RGB", (32, 32))
        for fmt in ("WEBP", "PNG", "JPEG"):
            im.save(io.BytesIO(), fmt)
    except Exception:
        pass


def selftest():
    import tempfile
    from PIL import Image
    fails = 0
    def ok(c, what):
        nonlocal fails
        print(("PASS " if c else "FAIL ") + what); fails += 0 if c else 1
    T = templates()
    ok(len(T) >= 40, "templates loaded: %d (images present in %s)" % (len(T), TPL_DIR))
    ok(os.path.isfile(font_path()), "font: %s" % font_path())
    ok(SAVE_SRC != "", "save pipeline: " + SAVE_SRC)
    for name, want in (("Drake", "drake"), ("distracted boyfriend", "db"), ("two buttons", "ds"),
                       ("expanding brain", "gb"), ("this is fine", "fine"), ("pooh duality", "pooh"), ("change my mind", "cmm")):
        r = resolve(name); ok(r is not None and r["id"] == want, "resolve %r -> %s" % (name, want))
    ok(resolve("zzzz qqqq") is None, "unknown template -> None")
    with tempfile.TemporaryDirectory() as d:
        r = make_meme_impl("drake", "doing homework", "playing with robot", out_dir=d)
        ok(r.get("ok"), "make_meme drake ok")
        if r.get("ok"):
            ok(Image.open(r["screen_png"]).size == (320, 240), "320x240 PNG written")
            ok(Image.open(r["screen_jpeg"]).format == "JPEG", "320x240 JPEG written")
            ok(r["original"].endswith(".webp") and Image.open(r["original"]).format == "WEBP", "original is WebP")
            ok(r["all_text_fits"], "text fits its boxes")
            r2 = make_meme_impl("drake", "doing homework", "playing with robot", out_dir=d)
            ok(r2.get("ok") and r2["ms"]["total"] < 300, "warm make_meme %.0f ms < 300 (first, cold: %.0f ms)" % (r2["ms"]["total"], r["ms"]["total"]))
        r = make_meme_impl("gb", lines=["one sock", "two socks", "matching socks", "no socks at all " * 5], out_dir=d)
        ok(r.get("ok") and r["all_text_fits"], "4-box template, long last line shrinks to fit")
        r = make_meme_impl("drake", "this is damn hard", "x", out_dir=d)
        ok(not r.get("ok") and r.get("blocked"), "blocked word refused")
        global BLOCK
        _saved, BLOCK = BLOCK, _block_re("Sam")
        r = make_meme_impl("drake", "Sam's homework", "x", out_dir=d)
        BLOCK = _saved
        ok(not r.get("ok") and r.get("blocked"), "family name from NG_MEME_BLOCKED_NAMES refused")
        r = make_meme_impl("not-a-template", "a", "b", out_dir=d)
        ok(not r.get("ok") and "unknown template" in r["error"], "unknown template refused with suggestions")
        r = make_meme_impl("drake", "", "", out_dir=d); ok(not r.get("ok"), "empty caption refused")
        r = make_meme_impl("drake", "x" * 500, "y", out_dir=d); ok(r.get("ok") or "too long" in r.get("error", ""), "very long caption handled")
        r = list_templates_impl("cat"); ok(r["count"] >= 2, "list_templates('cat') finds %d" % r["count"])
    print("ALL PASS" if not fails else "%d FAILED" % fails)
    return 1 if fails else 0


def main():
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    if "--make" in sys.argv:
        a = sys.argv[sys.argv.index("--make") + 1:]
        lines = a[a.index("--lines") + 1] if "--lines" in a else None
        out = a[a.index("--out") + 1] if "--out" in a else None
        pos = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or a[i - 1] not in ("--lines", "--out"))]
        print(json.dumps(make_meme_impl(pos[0], *(pos[1:3]), lines=lines, out_dir=out), indent=1)); return
    from typing import List, Optional
    from mcp.server.fastmcp import FastMCP
    mcp = FastMCP("nousgirl_localmeme")
    warm()                                      # warm caches so the first meme is fast

    @mcp.tool()
    def list_templates(query: str = "") -> str:
        """List the classic meme templates on your computer (about 60: drake, db = distracted boyfriend,
        ds = two buttons, gb = expanding brain, fine = this is fine, woman-cat, pooh, success, cmm = change
        my mind, gru, pigeon, spongebob, surprised-pikachu ...). Optional query filters by name.
        Only call this if you don't already know a fitting template id."""
        return json.dumps(list_templates_impl(query))

    @mcp.tool()
    def make_meme(template: str, top_text: str = "", bottom_text: str = "",
                  lines: Optional[List[str]] = None, title: str = "") -> str:
        """Make a classic meme LOCALLY in well under a second and save it on your computer (NousGirl-memes:
        WebP original + 320x240 JPEG + PNG for the screen). template = an id or name, e.g. "drake",
        "distracted boyfriend". For 2-box templates use top_text and bottom_text. For templates with more
        boxes (db 3, ds 3, gb 4, gru 4, chair 6 ...) pass lines=[...] in box order. Keep captions short,
        punchy and kid-safe. title = a few words for the file name. It saves the meme; to show it on the
        robot screen call robot_show_meme with name "latest" afterwards."""
        return json.dumps(make_meme_impl(template, top_text, bottom_text, lines, title))

    mcp.run()


if __name__ == "__main__":
    main()
