// v3.6.7-dj2: 13-band equalizer of the PCM that is actually being played.
// v3.6.7-dj3: + bass beat detector, rotating color palette with beat flashes,
//             and the matching colors for her 12 RGB LEDs.
// Pure logic (no ESP-IDF, no LVGL), unit-tested on the host
// (build/firmware/v3.6.7-dj2/host-tests/test_eq.cc).
//
//   Analyzer  one 20 ms window in -> 13 band levels 0..255. 1024-point real
//             FFT (done as a 512-point complex FFT + split), Hann window, hop =
//             the 20 ms window (480 samples at 24 kHz), so each FFT sees the
//             newest 42.7 ms. Band power = sum of |X[k]|^2 over the band's bins,
//             in dB, plus a small treble tilt, scaled against an adaptive
//             reference (instant rise, slow fall) so quiet and loud songs both
//             fill the bars. Below kSilenceDb a band is 0.
//   Bars      the display side: per-band fast attack / slower decay, peak-hold
//             caps that wait then fall, an optional "pulse" wave, column colors.
//   Layout    13 columns along the bottom of the 320x240 screen, under her face.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hg_eq {

constexpr int kBands = 13;
constexpr float kLoHz = 60.0f;
constexpr float kHiHz = 12000.0f;
constexpr int kFftN = 1024;            // real FFT length (23.4 Hz bins at 24 kHz)
constexpr int kHalf = kFftN / 2;       // complex FFT length
constexpr int kLog2Half = 9;

// Band k spans [BandEdgeHz(k), BandEdgeHz(k+1)), log-spaced, 13 bands from
// 60 Hz to 12 kHz (ratio 1.503 = ~0.59 octave per band).
inline float BandEdgeHz(int k) {
    return kLoHz * std::pow(kHiHz / kLoHz, (float)k / (float)kBands);
}

struct BinRange { int lo, hi; };       // FFT bins [lo, hi), never empty

// Bins per band for this sample rate. Bands never overlap; a band narrower
// than one bin gets the nearest single bin. Bin 0 (DC) and Nyquist are never used.
inline void MakeBins(int rate, BinRange out[kBands]) {
    const float df = (float)rate / (float)kFftN;
    int prev = 1;
    for (int k = 0; k < kBands; ++k) {
        int lo = std::max(prev, (int)std::lround(BandEdgeHz(k) / df));
        int hi = (int)std::lround(BandEdgeHz(k + 1) / df);
        if (hi > kHalf) hi = kHalf;
        if (lo > kHalf - 1) lo = kHalf - 1;
        if (hi <= lo) hi = lo + 1;
        out[k] = {lo, hi};
        prev = hi;
    }
}

// Level scaling (dB relative to a full-scale sine in one bin, Hann window).
constexpr float kFullScaleDb = 138.5f;   // 20*log10(32767 * kFftN / 4)
constexpr float kSilenceDb = -78.0f;     // band below this = 0 (dither / fade-out)
constexpr float kRangeDb = 42.0f;        // bar height spans the top 42 dB
constexpr float kRefMinDb = -34.0f;      // never boost quieter than this
constexpr float kRefFallDb = 0.08f;      // per window = 4 dB/s
constexpr float kTiltDbPerBand = 0.8f;   // +9.6 dB at the top band (music rolls off)

// Bass beat (kick) detector, one step per 20 ms window. Spectral flux of the
// three lowest bands (60..204 Hz): the sum of their dB rises since the last
// window. A beat = flux above max(kFluxMinDb, kFluxRatio x its running mean),
// bass louder than kBeatFloorDb, and at least kRefractoryWins windows (260 ms)
// since the last beat, so never more than ~3.8 beats per second.
constexpr int kBeatBands = 3;
constexpr float kFluxMinDb = 6.0f;
constexpr float kFluxRatio = 2.2f;
constexpr float kBeatFloorDb = -52.0f;
constexpr int kRefractoryWins = 13;      // 13 x 20 ms = 260 ms
struct BeatDetector {
    float prev[kBeatBands] = {-120.0f, -120.0f, -120.0f};
    float mean_flux = 2.0f;
    int since = 1000;                    // windows since the last beat
    bool Step(const float* db) {
        float flux = 0.0f, loud = -200.0f;
        for (int k = 0; k < kBeatBands; ++k) {
            float d = std::max(db[k], -100.0f);
            flux += std::max(0.0f, d - prev[k]);
            prev[k] = d;
            loud = std::max(loud, d);
        }
        ++since;
        bool beat = since >= kRefractoryWins && loud > kBeatFloorDb &&
                    flux > std::max(kFluxMinDb, kFluxRatio * mean_flux);
        mean_flux = 0.95f * mean_flux + 0.05f * std::min(flux, 30.0f);
        if (beat) since = 0;
        return beat;
    }
    void Reset() { *this = BeatDetector(); }
};

class Analyzer {
public:
    // Tables for this sample rate. Call once (or again if the rate changes).
    void Init(int rate) {
        rate_ = rate;
        MakeBins(rate, bins_);
        const double kPi = 3.14159265358979323846;
        for (int i = 0; i < kHalf; ++i) {
            cos_[i] = (float)std::cos(2.0 * kPi * i / kFftN);
            sin_[i] = (float)std::sin(2.0 * kPi * i / kFftN);
        }
        for (int i = 0; i < kFftN; ++i)
            win_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFftN - 1)));
        for (int i = 0; i < kHalf; ++i) {
            int r = 0;
            for (int b = 0; b < kLog2Half; ++b) if (i & (1 << b)) r |= 1 << (kLog2Half - 1 - b);
            rev_[i] = (uint16_t)r;
        }
        Reset();
    }
    void Reset() {
        std::memset(hist_, 0, sizeof(hist_));
        ref_db_ = kRefMinDb;
        beat_.Reset();
    }
    int rate() const { return rate_; }
    const BinRange& bins(int k) const { return bins_[k]; }

    // Append `len` new samples (the newest window), analyze the last kFftN
    // samples and write 13 levels 0..255. len may be 0..kFftN.
    // Returns true when this window starts a bass beat.
    bool Push(const int16_t* pcm, size_t len, uint8_t out[kBands]) {
        if (len > (size_t)kFftN) { pcm += len - kFftN; len = kFftN; }
        std::memmove(hist_, hist_ + len, (kFftN - len) * sizeof(int16_t));
        std::memcpy(hist_ + (kFftN - len), pcm, len * sizeof(int16_t));
        Spectrum();
        float top = -200.0f;
        for (int k = 0; k < kBands; ++k) {
            double p = 0;
            for (int b = bins_[k].lo; b < bins_[k].hi; ++b) p += pow_[b];
            float db = 10.0f * std::log10((float)p + 1e-3f) - kFullScaleDb;
            db_[k] = db;
            tilted_[k] = db + kTiltDbPerBand * k;
            top = std::max(top, tilted_[k]);
        }
        ref_db_ = std::max(std::max(ref_db_ - kRefFallDb, top), kRefMinDb);
        for (int k = 0; k < kBands; ++k) {
            float v = db_[k] < kSilenceDb ? 0.0f : (tilted_[k] - (ref_db_ - kRangeDb)) / kRangeDb;
            v = std::min(1.0f, std::max(0.0f, v));
            out[k] = (uint8_t)std::lround(v * 255.0f);
        }
        return beat_.Step(db_);
    }
    float band_db(int k) const { return db_[k]; }   // last window, dBFS (tests)
    float ref_db() const { return ref_db_; }
    float power(int bin) const { return pow_[bin]; }   // last |X[bin]|^2 (tests)

private:
    // |X[k]|^2, k = 0..kHalf-1, of the windowed history: x is packed as
    // z[m] = x[2m] + i x[2m+1], a kHalf-point complex FFT, then split.
    void Spectrum() {
        for (int m = 0; m < kHalf; ++m) {
            int j = rev_[m];
            re_[j] = hist_[2 * m] * win_[2 * m];
            im_[j] = hist_[2 * m + 1] * win_[2 * m + 1];
        }
        for (int len = 2; len <= kHalf; len <<= 1) {
            const int half = len >> 1, step = (kHalf / len) * 2;   // twiddle index stride in cos_/sin_
            for (int i = 0; i < kHalf; i += len) {
                for (int j = 0; j < half; ++j) {
                    const float wr = cos_[j * step], wi = -sin_[j * step];
                    const int a = i + j, b = a + half;
                    const float tr = re_[b] * wr - im_[b] * wi;
                    const float ti = re_[b] * wi + im_[b] * wr;
                    re_[b] = re_[a] - tr; im_[b] = im_[a] - ti;
                    re_[a] += tr; im_[a] += ti;
                }
            }
        }
        pow_[0] = 0.0f;
        for (int k = 1; k < kHalf; ++k) {
            const int mk = kHalf - k;
            const float ar = re_[k], ai = im_[k], br = re_[mk], bi = -im_[mk];   // b = conj(Z[M-k])
            const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);          // even part
            const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
            const float orr = di, oi = -dr;                                     // odd = d / i
            const float wr = cos_[k], wi = -sin_[k];                            // e^{-2 pi i k / N}
            const float xr = er + orr * wr - oi * wi;
            const float xi = ei + orr * wi + oi * wr;
            pow_[k] = xr * xr + xi * xi;
        }
    }

    int rate_ = 24000;
    BinRange bins_[kBands] = {};
    int16_t hist_[kFftN] = {};
    float win_[kFftN] = {};
    float cos_[kHalf] = {}, sin_[kHalf] = {};
    uint16_t rev_[kHalf] = {};
    float re_[kHalf] = {}, im_[kHalf] = {};
    float pow_[kHalf] = {};
    float db_[kBands] = {}, tilted_[kBands] = {};
    float ref_db_ = kRefMinDb;
    BeatDetector beat_;
};

// ── display side ──
constexpr float kAttack = 0.65f;        // per frame: move 65% of the way up
constexpr float kDecayMul = 0.90f;      // per frame down: x0.90 then -0.012
constexpr float kDecaySub = 0.012f;     //   (full height to 0 in ~0.75 s at 25 fps)
constexpr int kPeakHoldFrames = 12;     // caps wait ~0.5 s ...
constexpr float kPeakFall = 0.025f;     // ... then fall (full height in ~1.6 s)

struct Bars {
    float level[kBands] = {};
    float peak[kBands] = {};
    int hold[kBands] = {};
    // target: 13 levels 0..255, or nullptr = silence (everything falls to 0).
    // pulse: 0..1 floor for every band (0 = none).
    void Step(const uint8_t* target, const float* pulse = nullptr) {
        for (int k = 0; k < kBands; ++k) {
            float t = target ? target[k] / 255.0f : 0.0f;
            if (pulse) t = std::max(t, pulse[k]);
            float& l = level[k];
            if (t > l) l += (t - l) * kAttack;
            else l = std::max(t, l * kDecayMul - kDecaySub);
            if (l < 0.002f) l = 0.0f;
            if (l >= peak[k]) { peak[k] = l; hold[k] = kPeakHoldFrames; }
            else if (hold[k] > 0) --hold[k];
            else peak[k] = std::max(l, peak[k] - kPeakFall);
        }
    }
    void Clear() { *this = Bars(); }
};

// "pulse" (self.dj.vu pulse / set list cue): a wave that rolls across the
// columns at 2 Hz. Fills out[] with 0..1 floors; false when over.
inline bool PulseWave(int64_t now_ms, int64_t until_ms, float out[kBands]) {
    if (now_ms >= until_ms) return false;
    for (int k = 0; k < kBands; ++k) {
        float ph = (float)(now_ms % 500) / 500.0f * 6.2832f - 0.45f * k;
        out[k] = 0.55f + 0.4f * std::sin(ph);
    }
    return true;
}

// Layout, screen pixels (320x240). Her face is roughly y 40..180 on the GIF;
// the columns sit below it, where the old turntable deck was.
constexpr int kColW = 20, kColGap = 4, kX0 = 6;
constexpr int kBottom = 236;            // bars grow up from here
constexpr int kMaxH = 50;               // tallest bar: top at y 186
constexpr int kCapH = 3;                // peak cap
inline int ColX(int k) { return kX0 + k * (kColW + kColGap); }
inline int BarH(float level) {
    int h = (int)std::lround(std::min(1.0f, std::max(0.0f, level)) * kMaxH);
    return h;
}
inline int CapY(float peak) {           // top of the cap (sits on the bar top)
    return kBottom - BarH(peak) - kCapH - 1;
}

// Column colors. palette = hg_vu::Palette as int:
// 0 classic (party: low warm -> high cool), 1 purple, 2 blue, 3 gold, 4 warm,
// 5 rainbow (the classic sweep, slowly rotating in time).
inline uint32_t MixRgb(uint32_t a, uint32_t b, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float x = ((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t;
        out |= (uint32_t)std::lround(x) << sh;
    }
    return out;
}
inline uint32_t HueRgb(float h) {
    h = h - std::floor(h);
    float r = std::fabs(h * 6 - 3) - 1, g = 2 - std::fabs(h * 6 - 2), b = 2 - std::fabs(h * 6 - 4);
    auto c = [](float v) { return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}
inline uint32_t ColumnColor(int palette, int k, int64_t now_ms) {
    float t = (float)k / (float)(kBands - 1);
    switch (palette) {
        case 1: return MixRgb(0x7C4DFF, 0xFF40FF, t);
        case 2: return MixRgb(0x2962FF, 0x00E5FF, t);
        case 3: return MixRgb(0xFF8F00, 0xFFF59D, t);
        case 4: return MixRgb(0xFF3D00, 0xFFD180, t);
        case 5: return HueRgb(0.75f * t + (float)(now_ms % 6000) / 6000.0f);
        case 0:
        default: return HueRgb(0.75f * t);   // red, orange, yellow, green, cyan, blue, violet
    }
}

// ── v3.6.7-dj3: rotating palette, beat flash, LED colors ──
// The 13 columns always show 13 different hues spread over kHueSpan of the
// color wheel (bass = phase, treble = phase + kHueSpan). The phase turns once
// every kCycleMs (rainbow hint: kRainbowCycleMs). On a beat: a soft
// brightness flash (toward white, decays in ~0.2 s) and a 30 degree hue step,
// eased in over ~80 ms. Flashes are at least kMinFlashGapMs apart (<= 4 per
// second, photosensitivity), and never a hard on/off: the flash only fades.
// A palette hint (self.dj.vu color / set list "vu" cue) eases the phase to
// that color family; it keeps turning from there.
constexpr float kHueSpan = 0.75f;
constexpr float kCycleMs = 10000.0f;
constexpr float kRainbowCycleMs = 5000.0f;
constexpr int64_t kMinFlashGapMs = 250;
constexpr float kBeatHueStep = 1.0f / 12.0f;
constexpr float kFlashDecayMs = 140.0f;  // e-folding; visible ~0.2-0.3 s
constexpr float kFlashWhite = 0.35f;     // flash mixes at most 35% toward white
constexpr float kBaseValue = 0.82f;      // bars at rest: 82% value, flash -> 100%
// LEDs: never brighter than kLedMax per channel (44% of 255; the old party
// rainbow peaked at ~91%, the default state colors at 128).
constexpr int kLedMax = 112;
constexpr int kLedCount = 12;            // left 0..5, right 6..11 (mirrored)

inline float Wrap01(float x) { return x - std::floor(x); }
inline uint32_t ScaleRgb(uint32_t c, float v) {
    v = std::min(1.0f, std::max(0.0f, v));
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8)
        out |= (uint32_t)std::lround(((c >> sh) & 0xFF) * v) << sh;
    return out;
}
inline float HintHue(int palette) {      // hg_vu::Palette as int
    switch (palette) {
        case 1: return 0.76f;            // purple
        case 2: return 0.58f;            // blue
        case 3: return 0.11f;            // gold
        case 4: return 0.05f;            // warm
        default: return 0.0f;            // classic: red bass -> violet treble
    }
}

struct ColorState {
    float phase = 0.0f;                  // hue of the bass column, 0..1
    float jump = 0.0f;                   // beat hue step still to apply
    float flash = 0.0f;                  // 0..1
    float cycle_ms = kCycleMs;
    float target = -1.0f;                // hint hue, -1 = none
    int64_t last_ms = -1;
    int64_t last_flash_ms = -100000;
    int flashes = 0;

    void Hint(int palette) {
        if (palette == 5) { cycle_ms = kRainbowCycleMs; target = -1.0f; return; }
        cycle_ms = kCycleMs;
        target = HintHue(palette);
    }
    // Advance to now_ms. beat: a beat started since the last step.
    // Returns true if a flash started.
    bool Step(int64_t now_ms, bool beat) {
        float dt = last_ms < 0 ? 0.0f : (float)std::min<int64_t>(500, std::max<int64_t>(0, now_ms - last_ms));
        last_ms = now_ms;
        phase = Wrap01(phase + dt / cycle_ms);
        if (target >= 0.0f) {
            target = Wrap01(target + dt / cycle_ms);          // the hint turns too
            float d = Wrap01(target - phase + 0.5f) - 0.5f;   // shortest way round
            if (std::fabs(d) < 0.01f) { phase = target; target = -1.0f; }
            else phase = Wrap01(phase + d * std::min(1.0f, dt / 400.0f));
        }
        if (jump > 0.0f) {
            float a = std::min(jump, jump * std::min(1.0f, dt / 80.0f) + 0.002f);
            phase = Wrap01(phase + a);
            jump -= a;
        }
        flash *= std::exp(-dt / kFlashDecayMs);
        if (flash < 0.01f) flash = 0.0f;
        if (beat && now_ms - last_flash_ms >= kMinFlashGapMs) {
            last_flash_ms = now_ms;
            flash = 1.0f;
            jump += kBeatHueStep;
            ++flashes;
            return true;
        }
        return false;
    }
    uint32_t Column(int k) const {
        float t = (float)k / (float)(kBands - 1);
        uint32_t c = ScaleRgb(HueRgb(phase + kHueSpan * t), kBaseValue + (1.0f - kBaseValue) * flash);
        return MixRgb(c, 0xFFFFFF, kFlashWhite * flash);
    }
    uint32_t Cap(int k) const { return MixRgb(Column(k), 0xFFFFFF, 0.65f); }
    // LED i (0..11): the palette spread over each side's 6 LEDs (bass .. treble),
    // both sides the same. energy 0..1 = how loud (mean bar level).
    // Brightness = kLedMax x (0.35 + 0.35 energy + 0.30 flash), never above kLedMax.
    void Leds(float energy, uint32_t out[kLedCount]) const {
        energy = std::min(1.0f, std::max(0.0f, energy));
        float v = (0.35f + 0.35f * energy + 0.30f * flash) * (float)kLedMax / 255.0f;
        for (int i = 0; i < kLedCount; ++i) {
            int j = i % 6;
            out[i] = ScaleRgb(HueRgb(phase + kHueSpan * (float)j / 5.0f), v);
        }
    }
};

}  // namespace hg_eq
