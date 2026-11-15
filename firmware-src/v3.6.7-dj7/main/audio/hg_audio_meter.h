// headphone-girl v3: loudness meter of the PCM that is actually being played.
// AudioService::AudioOutputTask() calls Publish() right before each chunk goes
// to the codec. Readers (lip sync, dance-to-music) call Sample() at any time.
// Lock-free; a torn read only costs one slightly wrong mouth frame.
#pragma once
#include <esp_timer.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hg_meter {

constexpr int kWinMs = 20;      // one value per 20 ms of audio
constexpr int kMaxWins = 16;    // up to 320 ms per chunk

struct State {
    std::atomic<int64_t> start_us{0};     // when this chunk started playing
    std::atomic<int> wins{0};
    std::atomic<uint16_t> rms[kMaxWins];
    std::atomic<uint8_t> zcr[kMaxWins];   // zero crossings per 1000 samples (capped)
    // v3.5: vowel per window, filled a few ms later by the hg_lip task.
    std::atomic<uint32_t> seq{0};         // chunk counter, bumped by Publish()
    std::atomic<uint32_t> vseq{0};        // chunk the vowel[] values belong to
    std::atomic<uint8_t> vowel[kMaxWins]; // hg_vowel::Vowel as a number
    std::atomic<uint8_t> vconf[kMaxWins]; // confidence 0..255
    // Optional consumer of the raw chunk (the vowel task). Must not block.
    std::atomic<void (*)(const int16_t*, size_t, int, uint32_t)> tap{nullptr};
    // v3.6.7-dj2: second consumer (the DJ equalizer task, hg_eq_task.cc). Must not block.
    std::atomic<void (*)(const int16_t*, size_t, int, uint32_t)> tap2{nullptr};
};
inline State& Get() { static State s; return s; }

inline void Publish(const int16_t* pcm, size_t samples, int sample_rate) {
    if (pcm == nullptr || samples == 0 || sample_rate <= 0) return;
    auto& s = Get();
    size_t win = (size_t)sample_rate * kWinMs / 1000;
    if (win == 0) return;
    int n = 0;
    for (size_t off = 0; off < samples && n < kMaxWins; off += win, ++n) {
        size_t end = off + win < samples ? off + win : samples;
        double acc = 0; int zc = 0;
        for (size_t i = off; i < end; ++i) {
            acc += (double)pcm[i] * pcm[i];
            if (i > off && ((pcm[i] >= 0) != (pcm[i - 1] >= 0))) ++zc;
        }
        size_t len = end - off;
        s.rms[n].store((uint16_t)std::sqrt(acc / (len ? len : 1)));
        int z = (int)((int64_t)zc * 1000 / (int64_t)(len ? len : 1));   // per 1000 samples
        s.zcr[n].store((uint8_t)(z > 255 ? 255 : z));
    }
    s.wins.store(n);
    uint32_t q = s.seq.load() + 1;
    s.seq.store(q);
    s.start_us.store(esp_timer_get_time());
    if (auto tap = s.tap.load()) tap(pcm, samples, sample_rate, q);
    if (auto tap2 = s.tap2.load()) tap2(pcm, samples, sample_rate, q);
}

// rms/zcr of the 20 ms window playing now. Returns false if nothing played
// in the last `stale_ms` (silence).
inline bool Sample(uint16_t* rms, uint8_t* zcr, int stale_ms = 150) {
    auto& s = Get();
    int64_t t0 = s.start_us.load();
    int n = s.wins.load();
    if (t0 == 0 || n == 0) return false;
    int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
    int idx = (int)(dt_ms / kWinMs);
    if (dt_ms > (int64_t)n * kWinMs + stale_ms) return false;
    if (idx >= n) idx = n - 1;
    if (idx < 0) idx = 0;
    *rms = s.rms[idx].load();
    if (zcr) *zcr = s.zcr[idx].load();
    return true;
}

// v3.5: also the vowel of that window (0 = unknown or not analysed yet).
inline bool Sample(uint16_t* rms, uint8_t* zcr, uint8_t* vowel, uint8_t* vconf,
                   int stale_ms = 150) {
    auto& s = Get();
    int64_t t0 = s.start_us.load();
    int n = s.wins.load();
    uint32_t q = s.seq.load();
    if (!Sample(rms, zcr, stale_ms)) return false;
    int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
    int idx = (int)(dt_ms / kWinMs);
    if (idx >= n) idx = n - 1;
    if (idx < 0) idx = 0;
    if (s.vseq.load() == q && idx < kMaxWins) {
        *vowel = s.vowel[idx].load();
        *vconf = s.vconf[idx].load();
    } else {
        *vowel = 0;
        *vconf = 0;
    }
    return true;
}

}  // namespace hg_meter
