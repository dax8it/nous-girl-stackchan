"""Local OpenAI-compatible speech-to-text for Nous-Girl (StackChan).

POST /v1/audio/transcriptions  (multipart: file, optional model/language/response_format)
GET  /health                    model, load time, memory
Binds 127.0.0.1 only. Model chosen at start by STT_MODEL:
  parakeet (default) -> mlx-community/parakeet-tdt-0.6b-v3 via parakeet-mlx
  whisper            -> mlx-community/whisper-large-v3-turbo-asr-fp16 via mlx-audio
The model is loaded once at startup and warmed with 1 s of silence, then kept in memory.
No auth (localhost only); any Authorization header is ignored. Audio is written to a temp
file, transcribed, and deleted. Nothing is logged except timings and text length.
"""
import os, time, tempfile, threading, logging
import numpy as np
import mlx.core as mx
from fastapi import FastAPI, UploadFile, File, Form, HTTPException
from fastapi.responses import JSONResponse, PlainTextResponse
import uvicorn

MODEL = os.environ.get("STT_MODEL", "parakeet").strip().lower()
LANG = os.environ.get("STT_LANGUAGE", "en")
HOST = os.environ.get("STT_HOST", "127.0.0.1")
PORT = int(os.environ.get("STT_PORT", "8662"))
REPOS = {
    "parakeet": os.environ.get("STT_PARAKEET_REPO", "mlx-community/parakeet-tdt-0.6b-v3"),
    "whisper": os.environ.get("STT_WHISPER_REPO", "mlx-community/whisper-large-v3-turbo-asr-fp16"),
}
if MODEL not in REPOS:
    raise SystemExit(f"STT_MODEL must be parakeet or whisper, got {MODEL!r}")

log = logging.getLogger("local-stt")
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
lock = threading.Lock()
state = {"model": MODEL, "repo": REPOS[MODEL], "load_s": None, "requests": 0}

def _load():
    t = time.time()
    if MODEL == "parakeet":
        from parakeet_mlx import from_pretrained
        m = from_pretrained(REPOS[MODEL])
    else:
        from mlx_audio.stt.utils import load_model
        m = load_model(REPOS[MODEL])
    state["load_s"] = round(time.time() - t, 2)
    return m

def _transcribe(path: str) -> str:
    if MODEL == "parakeet":
        return engine.transcribe(path).text.strip()
    r = engine.generate(path, language=LANG, temperature=0.0,
                        condition_on_previous_text=False, verbose=None)
    return (getattr(r, "text", "") or "").strip()

import re
# Her name gets heard as "newsgirl"/"noose girl"; normalize so Hermes sees "Nous-Girl".
# STT_NAME_FIX=0 disables.
NAME_FIX = os.environ.get("STT_NAME_FIX", "1") != "0"
NAME_RE = re.compile(r"\b(?:(?:news|noose|nous|noos|nooce|newts|nuse|nice|newest)[\s-]?girl|new[\s-]?us[\s-]?girl|news[\s-]?crow)\b", re.I)
# Nous Research gets heard as "news research"/"new us research".
RESEARCH_RE = re.compile(r"\b(?:news|noose|nous|noos|nooce|nuse|new[\s-]?us)[\s-]?research\b", re.I)
# Whisper invents these on silence/noise; drop them only when they are the whole transcript.
HALLU = {"thank you", "thank you.", "thanks for watching", "thanks for watching!",
         "thanks for watching.", "you", "bye.", "bye", ".", "thank you very much."}


# --- multilingual gate: name fix only when the text is English or unclear (staged-multilingual) ---
# Tiny dependency-free language guesser for en/es/fr/de/pt (Nous-Girl multilingual).
# Returns (lang, confident). Short or ambiguous text -> (None, False) so callers keep the last language.
_W = re.compile(r"[a-zA-ZÀ-ÿœŒß']+")
_STOP = {
 "en": "the and you is are to of it that this what was for with have do not my your i me we can just like so yeah okay hey hi hello there how why where when be oh wow cool its it's i'm don't let's".split(),
 "es": "el la los las y es son que de en un una por para con no sí si yo tú tu mi me te se lo le qué cómo dónde cuándo muy pero hola gracias bueno vamos está estás eres soy hay también".split(),
 "fr": "le la les et est sont que de des du un une pour avec pas oui non je tu il elle nous vous mon ma mes ton ta qui quoi comment où quand très mais bonjour merci c'est ça suis es avez".split(),
 "de": "der die das und ist sind dass zu von ein eine für mit nicht ja nein ich du er sie wir ihr mein dein was wie wo wann sehr aber hallo danke auch bin bist hast gut".split(),
 "pt": "o a os as e é são que de do da em um uma por para com não sim eu você tu meu minha o que como onde quando muito mas olá obrigado obrigada também estou está tem".split(),
}
_STOP = {k: set(v) for k, v in _STOP.items()}
_CHARS = {"es": "ñ¿¡", "fr": "œèêëîïûù", "de": "ßäöü", "pt": "ãõ"}

def detect(text):
    t = (text or "").lower()
    words = _W.findall(t)
    score = {k: sum(1 for w in words if w in s) for k, s in _STOP.items()}
    for k, cs in _CHARS.items():
        score[k] += 2 * sum(t.count(c) for c in cs)
    if "ç" in t:
        score["fr"] += 1; score["pt"] += 1
    best = max(score, key=score.get)
    ranked = sorted(score.values(), reverse=True)
    if ranked[0] >= 2 and ranked[0] - ranked[1] >= 1:
        return best, True
    if ranked[0] == 1 and ranked[1] == 0 and len(words) <= 3:
        return best, False
    return None, False

def _name_fix_ok(text: str) -> bool:
    lang, confident = detect(text)
    return lang == "en" or not confident
# --- end multilingual gate ---

def _clean(text: str) -> str:
    t = text.strip()
    if MODEL == "whisper" and t.lower() in HALLU:
        return ""
    if NAME_FIX and _name_fix_ok(t):
        t = NAME_RE.sub("Nous-Girl", t)
        t = RESEARCH_RE.sub("Nous Research", t)
    return t

engine = _load()
with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:  # warm-up
    import wave
    w = wave.open(f.name, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    w.writeframes(np.zeros(16000, dtype=np.int16).tobytes()); w.close()
    t = time.time(); _transcribe(f.name); state["warmup_s"] = round(time.time() - t, 2)
    os.unlink(f.name)
log.info("loaded %s in %ss, warmup %ss", REPOS[MODEL], state["load_s"], state["warmup_s"])

app = FastAPI(title="local-stt")

@app.get("/health")
def health():
    mem = {}
    for k in ("get_active_memory", "get_peak_memory", "get_cache_memory"):
        fn = getattr(mx, k, None)
        if fn: mem[k[4:] + "_mb"] = round(fn() / 1e6, 1)
    return {"ok": True, **state, "mlx": mem}

@app.get("/v1/models")
def models():
    return {"object": "list", "data": [{"id": MODEL, "object": "model", "owned_by": "local"}]}

@app.post("/v1/audio/transcriptions")
async def transcriptions(file: UploadFile = File(...), model: str = Form(None),
                         language: str = Form(None), response_format: str = Form("json")):
    data = await file.read()
    if not data:
        raise HTTPException(400, "empty file")
    suffix = os.path.splitext(file.filename or "")[1] or ".wav"
    with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as f:
        f.write(data); p = f.name
    try:
        t = time.time()
        with lock:
            text = _clean(_transcribe(p))
        dt = time.time() - t
    except Exception as e:
        log.exception("transcribe failed")
        raise HTTPException(500, f"transcribe failed: {type(e).__name__}")
    finally:
        os.unlink(p)
    state["requests"] += 1
    log.info("%s %d bytes -> %d chars in %.3fs", MODEL, len(data), len(text), dt)
    if response_format == "text":
        return PlainTextResponse(text)
    return JSONResponse({"text": text, "model": MODEL, "duration_s": round(dt, 3)})

if __name__ == "__main__":
    uvicorn.run(app, host=HOST, port=PORT, log_level="warning")
