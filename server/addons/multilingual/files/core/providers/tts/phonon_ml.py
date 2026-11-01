"""PhononMLTTS: Phonon 40M multilingual, one ptts-openai-server per language (Nous-Girl).

Phonon picks its text normalization per SERVER (--lang), not per request, so each reply
sentence is language-guessed here and sent to that language's server:
  lang_urls: {en: http://host.docker.internal:8661/..., es: ...:8681, fr: ...:8682, de: ...:8683, pt: ...:8684}
Short/ambiguous sentences ("Wow!", "Okay.") keep the previous sentence's language.
If a language server fails, it falls back to en, then to fallback_url (the old 31m :8660).
The spoken "Nous" -> "Nooce" fix only runs for English (French "nous" is a real word).
Staged 2026-10-24. Remove this file and select PhononTTS again to undo.
"""
import re
import requests
from core.providers.tts.openai import TTSProvider as OpenAITTSProvider
from config.logger import setup_logging

TAG = __name__
logger = setup_logging()
SPOKEN_FIXES_EN = [(re.compile(r"\bnous\b", re.I), "Nooce")]

import re
_W = re.compile(r"[a-zA-ZÀ-ÿœŒß']+")
# English disfluencies, ignored before guessing (2026-11-01: "um" counted as Portuguese).
_FILLER_RX = re.compile(r"\b(?:u+m+|u+h+|e+r+m*|h+m+|m+h*m+|a+h+|o+h+)\b[,.]?", re.I)
# Ambiguous short words (also common English) are left out of the foreign lists.
_STOP = {
 "en": "the a an and you is are to of it that this what was for with have do does not my your i me we can just like so yeah okay hey hi hello there how why where when be wow cool its it's i'm don't let's about song story play who little boy ate time please some want sing dance tell read".split(),
 "es": "el los las es son que en una por para con sí yo tú mi qué cómo dónde cuándo muy pero hola gracias bueno vamos está estás eres soy hay también puedes canción cantar un lo te y ya dame déjame digo mientras busco estoy".split(),
 "fr": "le les et est sont que des du une pour avec pas oui non je tu il elle nous vous mon mes ton qui quoi comment où quand très mais bonjour merci c'est ça suis avez peux chanson chanter est-ce instant dans jette coup d'oeil mémoire".split(),
 "de": "der das und ist sind dass zu von ein eine für mit nicht ja nein ich du sie wir ihr mein dein wie wo wann sehr aber hallo danke auch bin bist hast gut kannst mir lied".split(),
 "pt": "os é são que da em uma por para com não sim eu você meu minha como onde quando muito mas olá obrigado obrigada também estou está tem música pode só vou dá enquanto procuro pouquinho tentando lembrar".split(),
}
_STOP = {k: set(v) for k, v in _STOP.items()}
_CHARS = {"es": "ñ¿¡", "fr": "œèêëîïûù", "de": "ßäöü", "pt": "ãõ"}

def strip_fillers(text):
    return re.sub(r"\s{2,}", " ", _FILLER_RX.sub(" ", text or "")).strip()

def detect(text):
    """Returns (lang, confident). Leaves English only on real evidence: a language's
    special letters, or 2+ marker words that beat the English count. Latin text with no
    foreign markers (2+ words) is confidently English, so the TTS switches back at once."""
    t = strip_fillers(text).lower()
    words = _W.findall(t)
    if not words:
        return None, False
    score = {k: sum(1 for w in words if w in s) for k, s in _STOP.items()}
    chars = {k: sum(t.count(c) for c in cs) for k, cs in _CHARS.items()}
    if "ç" in t:
        chars["fr"] += 1; chars["pt"] += 1
    en = score["en"]
    foreign = {k: score[k] + 2 * chars[k] for k in _CHARS}
    best = max(foreign, key=foreign.get)
    fb = foreign[best]
    others = sorted(foreign.values(), reverse=True)[1]
    if fb > others and (chars[best] > 0 or (score[best] >= 2 and fb > en)):
        return best, True
    if fb == 0 or (en >= 2 and en > fb):
        if len(words) >= 2 or en >= 1:
            return "en", True
    return None, False

class TTSProvider(OpenAITTSProvider):
    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self.lang_urls = dict(config.get("lang_urls") or {})
        self.lang_urls.setdefault("en", self.api_url)
        self.lang_voices = dict(config.get("lang_voices") or {})
        self.fallback_url = config.get("fallback_url") or ""
        self.default_lang = config.get("default_lang", "en")
        self.last_lang = self.default_lang
        self.last_used = None
        logger.bind(tag=TAG).info(
            f"PhononML ready: langs={sorted(self.lang_urls)} fallback={'yes' if self.fallback_url else 'no'}")

    def pick_lang(self, text):
        lang, confident = detect(text)
        if confident and lang in self.lang_urls:
            self.last_lang = lang
        return self.last_lang

    def _post(self, url, data, headers):
        return requests.post(url, json=data, headers=headers, timeout=self.tts_timeout)

    async def text_to_speak(self, text, output_file):
        lang = self.pick_lang(text)
        spoken = text
        if lang == "en":
            for rx, rep in SPOKEN_FIXES_EN:
                spoken = rx.sub(rep, spoken)
        headers = {"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"}
        data = {"model": self.model, "input": spoken, "voice": self.lang_voices.get(lang, self.voice),
                "response_format": self.audio_file_type, "speed": self.speed}
        tries = [(lang, self.lang_urls.get(lang))]
        if lang != "en":
            tries.append(("en", self.lang_urls.get("en")))
        if self.fallback_url:
            tries.append(("fallback", self.fallback_url))
        last_err = None
        self.last_used = None
        for which, url in tries:
            if not url:
                continue
            if which != lang:
                data["voice"] = self.lang_voices.get("en", self.voice)
            try:
                r = self._post(url, data, headers)
                if r.status_code == 200:
                    self.last_used = which
                    logger.bind(tag=TAG).info(f"PhononML {lang} via {which}: {len(r.content)} bytes")
                    if which != lang:
                        logger.bind(tag=TAG).warning(f"PhononML {lang} failed, used {which}")
                    if output_file:
                        with open(output_file, "wb") as f:
                            f.write(r.content)
                        return None
                    return r.content
                last_err = f"{which} HTTP {r.status_code}: {r.text[:200]}"
            except Exception as e:
                last_err = f"{which} {type(e).__name__}: {e}"
        raise Exception(f"PhononML TTS failed: {last_err}")
