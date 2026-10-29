#!/usr/bin/env python3
"""Build the local meme template pack (run once, needs network; you can re-run any time).
  uv run --no-project --quiet --python 3.12 --with pillow --with pyyaml python tools/build_templates.py
Writes:
  hermes/templates.json                ids, names, aliases, text boxes (fractions of the image), hints, source URLs
  $NG_TEMPLATES_DIR (default ~/NousGirl-meme-templates)/<id>.jpg|png   blank images, longest side <= 800 px
Images are NOT in the repo (copyrighted stills, used privately; see README). Boxes come from memegen.link's
per-template config.yml (MIT-licensed project) with hand overrides for a few panels; two imgflip blanks
use hand-measured boxes."""
import io, json, os, sys, time, urllib.request
import yaml
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_JSON = os.path.join(HERE, "..", "hermes", "templates.json")
IMG_DIR = os.path.expanduser(os.environ.get("NG_TEMPLATES_DIR", "~/NousGirl-meme-templates"))
MAXSIDE = 800
UA = {"User-Agent": "NousGirl-localmemes/1 (personal use)"}

# memegen id -> (aliases, hint). Kid-safe picks only (no guns, drugs, death, politics, real-people slaps).
MEMEGEN = {
 "drake": (["drake", "drake hotline bling", "drakeposting"], "box1 = thing he says no to, box2 = thing he likes"),
 "db": (["distracted boyfriend"], "box1 = the new shiny thing (girl in red), box2 = the boyfriend (you), box3 = what you should be doing (girlfriend)"),
 "ds": (["two buttons", "daily struggle"], "box1 = left button, box2 = right button, box3 = the sweating guy"),
 "cmm": (["change my mind"], "box1 = the hot take on the sign"),
 "gb": (["expanding brain", "galaxy brain"], "box1..box4 = smallest brain idea to most 'enlightened' idea"),
 "fine": (["this is fine"], "box1 = the situation, box2 = 'this is fine'"),
 "woman-cat": (["woman yelling at cat", "woman yelling at a cat"], "box1 = the angry woman, box2 = the calm cat's reply"),
 "pooh": (["tuxedo winnie the pooh", "pooh duality", "fancy pooh"], "box1 = plain way, box2 = fancy way"),
 "success": (["success kid"], "top/bottom: the small win"),
 "gru": (["gru's plan", "gru plan"], "box1 = step 1, box2 = step 2, box3 = the oops, box4 = same oops (realizing)"),
 "handshake": (["epic handshake"], "box1 = left arm, box2 = right arm, box3 = what they agree on"),
 "exit": (["left exit 12 off ramp", "exit ramp"], "box1 = straight road (sensible), box2 = exit (tempting), box3 = the car (you)"),
 "pigeon": (["is this a pigeon", "is this a butterfly"], "box1 = the guy, box2 = the butterfly, box3 = 'is this ...?'"),
 "spongebob": (["mocking spongebob"], "top/bottom: the quote, then the mocking version"),
 "buzz": (["x x everywhere", "buzz lightyear"], "top = 'X', bottom = 'X everywhere'"),
 "mordor": (["one does not simply"], "top = 'one does not simply', bottom = the hard thing"),
 "fry": (["futurama fry", "not sure if"], "top = 'not sure if ...', bottom = 'or ...'"),
 "harold": (["hide the pain harold"], "top/bottom: smiling through it"),
 "rollsafe": (["roll safe", "think about it"], "top/bottom: the 'smart' loophole"),
 "same": (["they're the same picture", "same picture"], "box1 = thing A, box2 = thing B, box3 = 'they're the same picture'"),
 "bus": (["two guys on a bus"], "box1 = the gloomy side, box2 = the happy side"),
 "balloon": (["running away balloon"], "box1 = the guy, box2 = the balloon he wants, box3 = what holds him back"),
 "reveal": (["scooby doo mask reveal", "scooby doo"], "box1 = 'let's see who this really is', box2 = the mask, box3 = unmasking, box4 = the real thing"),
 "panik-kalm-panik": (["panik kalm panik"], "box1 = panik, box2 = kalm, box3 = panik again"),
 "spiderman": (["spider-man pointing", "spiderman pointing"], "box1 = left spider-man, box2 = right spider-man"),
 "doge": (["doge"], "top/bottom: 'such ...', 'very ...'"),
 "cheems": (["cheems"], "top/bottom"),
 "stonks": (["stonks"], "top = the 'smart' move, bottom = 'stonks'"),
 "wonka": (["condescending wonka"], "top/bottom: sarcastic question"),
 "philosoraptor": (["philosoraptor"], "top/bottom: deep silly question"),
 "grumpycat": (["grumpy cat"], "top/bottom: grumpy no"),
 "interesting": (["most interesting man", "i don't always"], "top = 'I don't always ...', bottom = 'but when I do ...'"),
 "oprah": (["oprah you get a"], "top = 'you get a ...', bottom = 'everybody gets a ...'"),
 "kermit": (["but that's none of my business", "kermit tea"], "top/bottom: the observation"),
 "money": (["shut up and take my money"], "top/bottom"),
 "morpheus": (["morpheus", "what if i told you"], "top = 'what if I told you', bottom = the reveal"),
 "noidea": (["i have no idea what i'm doing", "science dog"], "top/bottom"),
 "both": (["why not both"], "top = the choice, bottom = 'why not both?'"),
 "regret": (["i immediately regret this decision"], "top/bottom"),
 "tried": (["at least you tried"], "top/bottom"),
 "yuno": (["y u no"], "top/bottom: 'X, Y U NO ...'"),
 "xy": (["x all the y", "all the things"], "top = 'X', bottom = 'all the Y'"),
 "ackbar": (["it's a trap"], "top/bottom"),
 "aag": (["ancient aliens"], "top/bottom: '... aliens'"),
 "afraid": (["afraid to ask andy"], "top/bottom"),
 "awkward": (["socially awkward penguin"], "top/bottom"),
 "chair": (["american chopper argument"], "box1..box6: the argument back and forth"),
 "home": (["we have food at home"], "box1 = kid's request, box2 = 'we have X at home', box3 = the sad X at home"),
 "midwit": (["midwit", "bell curve"], "box1 = simple left, box2 = overthinking middle, box3 = simple right"),
 "khaby-lame": (["khaby lame shrug"], "top/bottom: the obvious easy fix"),
 "headaches": (["types of headaches"], "box1 = the worst headache cause"),
 "millers": (["you guys are getting paid"], "box1..box4: the back and forth"),
 "glasses": (["peter parker's glasses"], "top/bottom"),
 "firsttry": (["first try"], "top/bottom"),
 "boat": (["i should buy a boat"], "top/bottom"),
 "right": (["anakin padme", "for the better right"], "box1 = Anakin's plan, box2 = Padme smiling, box3 = Anakin silent (can be empty), box4 = Padme 'for the better, right?', box5 = Padme worried 'right?'"),
 "sparta": (["this is sparta"], "top/bottom"),
 "fwp": (["first world problems"], "top/bottom"),
}
# hand overrides (x, y, w, h as fractions) where memegen's defaults sit on the face / wrong panel
OVERRIDE = {
 "drake": [(0.5, 0.0, 0.5, 0.5), (0.5, 0.5, 0.5, 0.5)],
 "pooh": [(0.38, 0.0, 0.62, 0.5), (0.38, 0.5, 0.62, 0.5)],
 "db": [(0.05, 0.68, 0.4, 0.15), (0.48, 0.36, 0.26, 0.13), (0.72, 0.58, 0.28, 0.15)],
}
# imgflip extras: id, aliases, boxes, hint
IMGFLIP = {
 "surprised-pikachu": ("155067746", ["surprised pikachu"], [(0.02, 0.01, 0.96, 0.13), (0.02, 0.14, 0.96, 0.13), (0.02, 0.27, 0.96, 0.12)],
                       "box1 = doing the silly thing, box2 = the obvious result, box3 = shocked reaction"),
 "monkey-puppet": ("148909805", ["monkey puppet", "awkward look monkey"], [(0.02, 0.01, 0.96, 0.34)],
                   "box1 = the awkward moment (one caption on the white top)"),
}


def get(url, tries=5):
    for i in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=30) as r:
                return r.read()
        except Exception as e:
            if i == tries - 1:
                raise
            time.sleep(3 * (i + 1))


def store(tid, data):
    img = Image.open(io.BytesIO(data)); img.seek(0)
    img = img.convert("RGB")
    img.thumbnail((MAXSIDE, MAXSIDE), Image.LANCZOS)
    path = os.path.join(IMG_DIR, tid + ".jpg")
    img.save(path, "JPEG", quality=90)
    return os.path.basename(path), img.size


def main():
    os.makedirs(IMG_DIR, exist_ok=True)
    meta = {m["id"]: m for m in json.load(urllib.request.urlopen(urllib.request.Request("https://api.memegen.link/templates", headers=UA), timeout=30))}
    old = {}
    if os.path.exists(OUT_JSON) and "--fresh" not in sys.argv:   # resumable: keep templates already built
        old = {t["id"]: t for t in json.load(open(OUT_JSON))["templates"] if os.path.exists(os.path.join(IMG_DIR, t["file"]))}
        for tid, (aliases, hint) in MEMEGEN.items():     # aliases / hints always come from this file
            if tid in old: old[tid].update(aliases=aliases, hint=hint)
            if tid in old and tid in OVERRIDE: old[tid]["boxes"] = [list(b) for b in OVERRIDE[tid]]
    out, fails = [], []
    for tid, (aliases, hint) in MEMEGEN.items():
        if tid in old and "styles" in old[tid]:
            out.append(old[tid]); continue
        if tid in old:   # older build without text colours: fetch just the config
            cfg = yaml.safe_load(get("https://raw.githubusercontent.com/jacebrowning/memegen/main/templates/%s/config.yml" % tid))
            old[tid]["colors"] = [(t.get("color") or "white") for t in (cfg.get("text") or [])][: len(old[tid]["boxes"])] or ["white"]
            old[tid]["styles"] = [(t.get("style") or "upper") for t in (cfg.get("text") or [])][: len(old[tid]["boxes"])] or ["upper"]
            out.append(old[tid]); continue
        try:
            m = meta[tid]
            cfg = yaml.safe_load(get("https://raw.githubusercontent.com/jacebrowning/memegen/main/templates/%s/config.yml" % tid))
            img_url = m["blank"]
            fname, size = store(tid, get(img_url))
            texts = cfg.get("text") or []
            boxes = OVERRIDE.get(tid) or [(t.get("anchor_x", 0.0), t.get("anchor_y", 0.0), t.get("scale_x", 1.0), t.get("scale_y", 0.2)) for t in texts]
            boxes = boxes[: max(1, int(m.get("lines") or len(boxes)))]
            styles = [(t.get("style") or "upper") for t in texts][: len(boxes)] or ["upper"]
            out.append({"id": tid, "name": m["name"], "aliases": aliases, "file": fname, "size": list(size),
                        "boxes": [[round(float(v), 4) for v in b] for b in boxes],
                        "upper": all(s != "none" for s in styles), "hint": hint,
                        "colors": [(t.get("color") or "white") for t in texts][: len(boxes)] or ["white"],
                        "styles": styles,
                        "source": {"image": img_url, "boxes": "https://github.com/jacebrowning/memegen/blob/main/templates/%s/config.yml" % tid,
                                   "info": cfg.get("source") or m.get("source")}})
            print("ok  ", tid, size, len(boxes), "boxes")
        except Exception as e:
            fails.append(tid); print("FAIL", tid, str(e)[:120])
    flip = {m["id"]: m for m in json.load(urllib.request.urlopen(urllib.request.Request("https://api.imgflip.com/get_memes", headers=UA), timeout=30))["data"]["memes"]}
    for tid, (fid, aliases, boxes, hint) in IMGFLIP.items():
        if tid in old:
            old[tid].update(boxes=[list(b) for b in boxes], hint=hint, upper=False, colors=["black"] * len(boxes),
                            styles=["none"] * len(boxes)); out.append(old[tid]); continue
        try:
            m = flip[fid]; fname, size = store(tid, get(m["url"]))
            out.append({"id": tid, "name": m["name"], "aliases": aliases, "file": fname, "size": list(size),
                        "boxes": [list(b) for b in boxes], "upper": False, "hint": hint, "colors": ["black"] * len(boxes),
                        "styles": ["none"] * len(boxes),
                        "source": {"image": m["url"], "boxes": "hand-measured", "info": "https://imgflip.com/memetemplate/" + fid}})
            print("ok  ", tid, size, len(boxes), "boxes")
        except Exception as e:
            fails.append(tid); print("FAIL", tid, str(e)[:120])
    json.dump({"built": time.strftime("%Y-%m-%d %H:%M %Z"), "images_dir": "~/NousGirl-meme-templates", "templates": out},
              open(OUT_JSON, "w"), indent=1)
    print("templates: %d ok, %d failed %s -> %s" % (len(out), len(fails), fails, os.path.normpath(OUT_JSON)))


if __name__ == "__main__":
    main()
