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
