# Run: docker exec -i -e PYTHONDONTWRITEBYTECODE=1 -w /opt/xiaozhi-esp32-server <container> python - < tests/selftest.py
# Loads the installed provider; reads clips from data/smart-turn/selftest/*.wav; saves nothing.
import sys, os, time, wave, types, importlib.util, collections
sys.dont_write_bytecode = True
sys.path.insert(0, "/opt/xiaozhi-esp32-server")
import numpy as np
D = "data/smart-turn"
spec = importlib.util.spec_from_file_location("st_provider", f"core/providers/vad/silero_smartturn.py")
st = importlib.util.module_from_spec(spec); spec.loader.exec_module(st)

def load(p):
    w = wave.open(p); assert w.getframerate() == 16000 and w.getnchannels() == 1
    return w.readframes(w.getnframes())

print("== model-only (8 s window, trailing 0.3 s silence) threads=1")
m1 = st.SmartTurnModel.get(f"{D}/smart-turn-v3.2-cpu.onnx", 1)
m1.predict(np.zeros(16000, np.float32))
for f in sorted(os.listdir(f"{D}/selftest")):
    if not f.endswith(".wav"): continue
    a = np.frombuffer(load(f"{D}/selftest/{f}"), np.int16).astype(np.float32) / 32768
    a = np.concatenate([a, np.zeros(4800, np.float32)])
    ts = []
    for _ in range(5):
        t = time.monotonic(); p = m1.predict(a); ts.append((time.monotonic() - t) * 1000)
    print(f"  {f:18s} prob={p:.3f} {'COMPLETE' if p>=0.5 else 'incomplete'}  latency median={sorted(ts)[2]:.0f}ms")

for th in (2, 4):
    mt = st.SmartTurnModel.get(f"{D}/smart-turn-v3.2-cpu.onnx", th); mt.predict(np.zeros(16000, np.float32))
    a = np.frombuffer(load(f"{D}/selftest/complete2.wav"), np.int16).astype(np.float32) / 32768
    ts = []
    for _ in range(7):
        t = time.monotonic(); mt.predict(a); ts.append((time.monotonic() - t) * 1000)
    print(f"  threads={th} latency median={sorted(ts)[3]:.0f}ms")
print("== full VAD loop (real Silero + smart-turn), 60 ms frames, base silence 300 ms")
cfg = {"type": "silero_smartturn", "threshold": 0.5, "threshold_low": 0.3,
       "model_dir": "models/snakers4_silero-vad", "min_silence_duration_ms": 300,
       "smart_turn_model": f"{D}/smart-turn-v3.2-cpu.onnx", "smart_turn_threshold": 0.5,
       "smart_turn_recheck_ms": 300, "smart_turn_max_wait_ms": 2500}
vad = st.VADProvider(cfg)
class Conn: pass
for label, en, sil in [("smart-turn, base 300 ms", True, 300), ("plain Silero 800 ms (today)", False, 800)]:
    vad.st_enabled = en; vad.silence_threshold_ms = sil; print("  --", label)
    for f in sorted(os.listdir(f"{D}/selftest")):
        if not f.endswith(".wav"): continue
        pcm = load(f"{D}/selftest/{f}") + b"\x00\x00" * 16000 * 4  # 4 s silence after
        c = Conn(); c.client_listen_mode = "auto"; c.client_audio_buffer = bytearray()
        c.client_voice_window = collections.deque(maxlen=5); c.client_have_voice = False
        c.client_voice_stop = False; c.last_is_voice = False; c.vad_last_voice_time = 0.0; c.asr_audio = []
        step = 960 * 2; speech_end = None; stop_at = None; t0 = time.time()
        for i in range(0, len(pcm), step):
            fr = pcm[i:i+step]
            hv = vad.is_vad(c, fr)
            c.asr_audio.append(fr)
            if hv: speech_end = time.time()
            if c.client_voice_stop:
                stop_at = time.time(); break
            # real-time pacing: one 60 ms frame per 60 ms
            nxt = t0 + (i // step + 1) * 0.06
            time.sleep(max(0, nxt - time.time()))
        gap = (stop_at - speech_end) * 1000 if stop_at and speech_end else None
        print(f"  {f:18s} stop {gap:.0f} ms after last voiced frame" if gap else f"  {f:18s} no stop")
