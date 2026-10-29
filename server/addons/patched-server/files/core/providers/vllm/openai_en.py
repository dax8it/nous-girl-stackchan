"""OpenAI-compatible VLLM provider without the forced Chinese reply (headphone-girl).

Installed by the patched-server add-on as core/providers/vllm/openai_en.py. Select a
VLLM block with `type: openai_en`.

Why: upstream core/providers/vllm/openai.py appends "(请使用中文回复)"
("reply in Chinese") to every photo question and cannot pass reasoning
controls, so a thinking model (gemma4:e4b on Ollama) takes ~12 s per photo
instead of ~1 s (measured 2026-10-23 on your computer).

Extra config keys: reply_language (default English), reasoning_effort
(default "none"; set "" to omit), max_tokens (default 300).
"""
import openai

from config.logger import setup_logging
from core.providers.vllm.base import VLLMProviderBase

TAG = __name__
logger = setup_logging()


class VLLMProvider(VLLMProviderBase):
    def __init__(self, config):
        self.model_name = config.get("model_name")
        self.api_key = config.get("api_key") or "none"
        self.base_url = config.get("base_url") or config.get("url")
        self.max_tokens = int(config.get("max_tokens") or 300)
        self.reply_language = config.get("reply_language") or "English"
        effort = config.get("reasoning_effort", "none")
        self.reasoning_effort = str(effort) if effort not in (None, "") else ""
        # v3.6.4: keep the vision model loaded between photos. Ollama unloads it
        # after 5 min; a cold load costs ~7 s per photo (7.6 s cold vs 0.6 s warm,
        # measured 2026-10-23). Set keep_alive: "" in the VLLM block to omit.
        ka = config.get("keep_alive", "30m")
        self.keep_alive = str(ka) if ka not in (None, "") else ""
        self.client = openai.OpenAI(api_key=self.api_key, base_url=self.base_url,
                                    timeout=float(config.get("timeout") or 40))

    def response(self, question, base64_image):
        prompt = (f"{question}\nAnswer in {self.reply_language}, in 1-3 short, "
                  "kid-friendly sentences. Describe only what is visible.")
        kwargs = {}
        if self.reasoning_effort:
            kwargs["reasoning_effort"] = self.reasoning_effort
        if self.keep_alive:
            kwargs["extra_body"] = {"keep_alive": self.keep_alive}
        try:
            r = self.client.chat.completions.create(
                model=self.model_name, stream=False, max_tokens=self.max_tokens,
                messages=[{"role": "user", "content": [
                    {"type": "text", "text": prompt},
                    {"type": "image_url",
                     "image_url": {"url": f"data:image/jpeg;base64,{base64_image}"}},
                ]}], **kwargs)
            return r.choices[0].message.content
        except Exception as e:
            logger.bind(tag=TAG).error(f"VLLM openai_en error: {e}")
            raise
