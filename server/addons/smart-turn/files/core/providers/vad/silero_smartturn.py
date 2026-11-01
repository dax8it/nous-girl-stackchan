"""Silero VAD + Pipecat smart-turn v3 end-of-turn detection.

Per-frame speech detection is identical to silero.py. The only change is the
moment Silero would end the turn (silence >= min_silence_duration_ms): instead
of stopping right away, the last 8 s of the turn audio is scored by the
smart-turn ONNX model in a worker thread.

  prob >= smart_turn_threshold -> end the turn (same as plain Silero)
  prob <  threshold            -> keep listening, re-score every recheck_ms
  silence >= max_wait_ms       -> force end of turn
  speech resumes               -> pending check discarded

Any smart-turn error falls back to plain Silero behaviour. No audio is saved.
Model: https://huggingface.co/pipecat-ai/smart-turn-v3 (BSD-2-Clause).
"""

import os
import time
import threading
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import onnxruntime

from config.logger import setup_logging
from core.providers.vad.silero import VADProvider as SileroVADProvider

TAG = __name__
logger = setup_logging()

SAMPLE_RATE = 16000
WINDOW_SECONDS = 8
N_SAMPLES = SAMPLE_RATE * WINDOW_SECONDS  # 128000
N_FFT = 400
HOP = 160
N_MELS = 80


# ---------------------------------------------------------------------------
# Whisper log-mel features in pure numpy (matches transformers'
# WhisperFeatureExtractor(chunk_length=8), do_normalize=True, so the container
# does not need transformers).
# ---------------------------------------------------------------------------
def _hz_to_mel(f):
    f = np.asarray(f, dtype=np.float64)
    mel = 3.0 * f / 200.0
    log_region = f >= 1000.0
    mel = np.where(
        log_region,
        15.0 + np.log(np.maximum(f, 1e-10) / 1000.0) * (27.0 / np.log(6.4)),
        mel,
    )
    return mel


def _mel_to_hz(m):
    m = np.asarray(m, dtype=np.float64)
    f = 200.0 * m / 3.0
    return np.where(m >= 15.0, 1000.0 * np.exp(np.log(6.4) / 27.0 * (m - 15.0)), f)


def _mel_filters():
    fft_freqs = np.linspace(0, SAMPLE_RATE // 2, N_FFT // 2 + 1)
    mel_pts = np.linspace(_hz_to_mel(0.0), _hz_to_mel(8000.0), N_MELS + 2)
    filt_freqs = _mel_to_hz(mel_pts)
    diff = np.diff(filt_freqs)
    slopes = filt_freqs[None, :] - fft_freqs[:, None]
    down = -slopes[:, :-2] / diff[:-1]
    up = slopes[:, 2:] / diff[1:]
    fb = np.maximum(0.0, np.minimum(down, up))
    enorm = 2.0 / (filt_freqs[2 : N_MELS + 2] - filt_freqs[:N_MELS])
    return fb * enorm[None, :]  # (201, 80)


_MEL_FB = _mel_filters()
_WINDOW = (0.5 - 0.5 * np.cos(2 * np.pi * np.arange(N_FFT) / N_FFT))  # periodic hann


def whisper_features(audio_f32):
    """audio_f32: 1-D float32 16 kHz. Returns (1, 80, 800) float32."""
    x = np.asarray(audio_f32, dtype=np.float32)
    # official inference.py: keep last 8 s, left-pad with zeros
    if len(x) > N_SAMPLES:
        x = x[-N_SAMPLES:]
    elif len(x) < N_SAMPLES:
        x = np.pad(x, (N_SAMPLES - len(x), 0))
    # do_normalize: zero mean, unit variance over the full 8 s window
    x = (x - x.mean()) / np.sqrt(x.var() + 1e-7)
    x = x.astype(np.float64)
    # STFT, center=True, reflect pad
    xp = np.pad(x, (N_FFT // 2, N_FFT // 2), mode="reflect")
    n_frames = 1 + (len(xp) - N_FFT) // HOP
    idx = np.arange(N_FFT)[None, :] + HOP * np.arange(n_frames)[:, None]
    frames = xp[idx] * _WINDOW[None, :]
    spec = np.abs(np.fft.rfft(frames, n=N_FFT, axis=1)) ** 2  # (frames, 201)
    mel = spec @ _MEL_FB  # (frames, 80)
    log_spec = np.log10(np.maximum(mel, 1e-10)).T[:, :-1]  # (80, 800)
    log_spec = np.maximum(log_spec, log_spec.max() - 8.0)
    log_spec = (log_spec + 4.0) / 4.0
    return log_spec[None, :, :].astype(np.float32)


class SmartTurnModel:
    """One ONNX session per process, shared by all connections."""

    _lock = threading.Lock()
    _instances = {}

    @classmethod
    def get(cls, path, threads):
        with cls._lock:
            key = (path, threads)
            if key not in cls._instances:
                cls._instances[key] = cls(path, threads)
            return cls._instances[key]

    def __init__(self, path, threads):
        so = onnxruntime.SessionOptions()
        so.execution_mode = onnxruntime.ExecutionMode.ORT_SEQUENTIAL
        so.inter_op_num_threads = 1
        so.intra_op_num_threads = int(threads)
        so.graph_optimization_level = onnxruntime.GraphOptimizationLevel.ORT_ENABLE_ALL
        self.session = onnxruntime.InferenceSession(
            path, sess_options=so, providers=["CPUExecutionProvider"]
        )
        self.input_name = self.session.get_inputs()[0].name
        self.run_lock = threading.Lock()

    def predict(self, audio_f32):
        feats = whisper_features(audio_f32)
        with self.run_lock:
            out = self.session.run(None, {self.input_name: feats})
        return float(np.asarray(out[0]).reshape(-1)[0])


class VADProvider(SileroVADProvider):
    def __init__(self, config):
        super().__init__(config)
        self.st_enabled = False
        self.st_threshold = float(config.get("smart_turn_threshold", 0.5) or 0.5)
        self.st_recheck_ms = int(config.get("smart_turn_recheck_ms", 300) or 300)
        self.st_max_wait_ms = int(config.get("smart_turn_max_wait_ms", 2500) or 2500)
        self.st_log = str(config.get("smart_turn_log", True)).lower() not in ("false", "0", "no")
        path = config.get("smart_turn_model", "data/smart-turn/smart-turn-v3.2-cpu.onnx")
        threads = int(config.get("smart_turn_threads", 1) or 1)
        try:
            self.st_model = SmartTurnModel.get(path, threads)
            self.st_executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="smartturn")
            # warm up once so the first real turn is not slow
            t0 = time.monotonic()
            self.st_model.predict(np.zeros(SAMPLE_RATE, dtype=np.float32))
            self.st_enabled = True
            logger.bind(tag=TAG).info(
                f"SmartTurn ready model={os.path.basename(path)} threshold={self.st_threshold} "
                f"base_silence={self.silence_threshold_ms}ms recheck={self.st_recheck_ms}ms "
                f"max_wait={self.st_max_wait_ms}ms warmup={(time.monotonic()-t0)*1000:.0f}ms"
            )
        except Exception as e:
            logger.bind(tag=TAG).error(f"SmartTurn disabled, plain Silero fallback: {e}")

    # -- per-connection smart-turn state ----------------------------------
    @staticmethod
    def _st_reset(conn):
        conn._st_future = None
        conn._st_checked_at = None  # silence ms when last check was submitted
        conn._st_last_prob = None

    def _st_submit(self, conn, silence_ms):
        pcm = b"".join(conn.asr_audio) + bytes(conn.client_audio_buffer)
        audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
        t0 = time.monotonic()
        fut = self.st_executor.submit(self.st_model.predict, audio)
        fut._st_t0 = t0
        fut._st_secs = len(audio) / SAMPLE_RATE
        conn._st_future = fut
        conn._st_checked_at = silence_ms

    def _st_should_stop(self, conn, silence_ms):
        """Called when Silero says the turn is over. True = end the turn."""
        if not self.st_enabled or conn.client_listen_mode == "manual":
            return True
        try:
            if not hasattr(conn, "_st_future"):
                self._st_reset(conn)
            if silence_ms >= self.st_max_wait_ms:
                if self.st_log:
                    logger.bind(tag=TAG).info(
                        f"SmartTurn force stop silence={silence_ms:.0f}ms last_prob={conn._st_last_prob}"
                    )
                self._st_reset(conn)
                return True
            fut = conn._st_future
            if fut is None:
                # first check of this pause, or time for a recheck
                if conn._st_checked_at is None or silence_ms - conn._st_checked_at >= self.st_recheck_ms:
                    self._st_submit(conn, silence_ms)
                return False
            if not fut.done():
                return False
            conn._st_future = None  # consume this verdict once
            prob = fut.result()  # raises -> plain Silero fallback below
            conn._st_last_prob = round(prob, 3)
            complete = prob >= self.st_threshold
            if self.st_log:
                logger.bind(tag=TAG).info(
                    f"SmartTurn prob={prob:.3f} {'COMPLETE' if complete else 'wait'} "
                    f"silence={silence_ms:.0f}ms audio={fut._st_secs:.1f}s "
                    f"latency={(time.monotonic()-fut._st_t0)*1000:.0f}ms"
                )
            if complete:
                self._st_reset(conn)
                return True
            return False
        except Exception as e:
            logger.bind(tag=TAG).error(f"SmartTurn error, stopping as plain Silero: {e}")
            self._st_reset(conn)
            return True

    def release_conn_resources(self, conn):
        super().release_conn_resources(conn)
        for attr in ("_st_future", "_st_checked_at", "_st_last_prob"):
            if hasattr(conn, attr):
                try:
                    delattr(conn, attr)
                except Exception:
                    pass

    # -- same loop as silero.py, with the stop decision routed through smart-turn
    def is_vad(self, conn, pcm_frame):
        if conn.client_listen_mode == "manual":
            return True

        try:
            self._init_connection_state(conn)
            conn.client_audio_buffer.extend(pcm_frame)

            client_have_voice = False
            while len(conn.client_audio_buffer) >= 512 * 2:
                chunk = conn.client_audio_buffer[: 512 * 2]
                conn.client_audio_buffer = conn.client_audio_buffer[512 * 2 :]

                audio_int16 = np.frombuffer(chunk, dtype=np.int16)
                audio_float32 = audio_int16.astype(np.float32) / 32768.0
                audio_input = np.concatenate(
                    [conn._vad_context, audio_float32.reshape(1, -1)], axis=1
                ).astype(np.float32)

                ort_inputs = {
                    "input": audio_input,
                    "state": conn._vad_state,
                    "sr": np.array(16000, dtype=np.int64),
                }
                out, state = self.session.run(None, ort_inputs)

                conn._vad_state = state
                conn._vad_context = audio_input[:, -64:]
                speech_prob = out.item()

                if speech_prob >= self.vad_threshold:
                    is_voice = True
                elif speech_prob <= self.vad_threshold_low:
                    is_voice = False
                else:
                    is_voice = conn.last_is_voice

                conn.last_is_voice = is_voice

                conn.client_voice_window.append(is_voice)
                client_have_voice = (
                    conn.client_voice_window.count(True) >= self.frame_window_threshold
                )

                if conn.client_have_voice and not client_have_voice:
                    stop_duration = time.time() * 1000 - conn.vad_last_voice_time
                    if stop_duration >= self.silence_threshold_ms and not conn.client_voice_stop:
                        if self._st_should_stop(conn, stop_duration):
                            conn.client_voice_stop = True
                if client_have_voice:
                    conn.client_have_voice = True
                    conn.vad_last_voice_time = time.time() * 1000
                    # speech resumed: drop any pending smart-turn verdict
                    if getattr(conn, "_st_checked_at", None) is not None:
                        self._st_reset(conn)

            return client_have_voice
        except Exception as e:
            logger.bind(tag=TAG).error(f"Error processing audio packet: {e}")
