// v3.6.7-dj4: rave mode = the DJ screen's background changes color with the music.
// Pure logic (no ESP-IDF, no LVGL), unit-tested on the host (host-tests/test_rave.cc).
//
// How it looks: her GIF face is black ink on white. In rave mode a full-screen
// tint is MULTIPLIED over the face (LVGL blend mode), so the white turns into
// a deep saturated color and her black lines stay black (a duotone). The EQ
// columns are drawn on top of the tint, unchanged.
//   hue    = palette phase + kHueOffset (the part of the color wheel no EQ
//            column uses at that moment, so the bars stand out), turning with
//            the palette, plus a bold kBeatHueStep jump on each pulse.
//   value  = kMinValue .. kMaxValue with loudness, + a beat pulse.
//
// Photosensitive safety (these hold for ANY input, see test_rave.cc):
//   - Pulses (beat flashes) at least kMinPulseGapMs apart: <= kMaxFlashesPerSec.
//   - The tint's relative luminance (WCAG) changes by at most
//     kMaxLumaStepPerSec per second. A WCAG "flash" needs two opposite changes
//     of >= 0.1, so it takes >= 2 x 0.1 / kMaxLumaStepPerSec = 364 ms:
//     never more than 2.75 general flashes per second, whatever the music does.
//   - Everything fades: the color eases toward its target (kColorEaseMs) and
//     never jumps; turning on / off fades from / to white (no tint) the same way.
//   - Never saturated red: the linear red ratio R/(R+G+B) stays below
//     kRedMaxRatio (WCAG "saturated red" starts at 0.8); near red the
//     saturation is lowered, so there is no saturated-red full-screen flash.
//   - Value never above kMaxValue.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hg_rave {

// ── tunables ──
constexpr float kMaxValue = 0.65f;          // brightest background (HSV value)
constexpr float kMinValue = 0.40f;          // quiet passages
constexpr float kSaturation = 0.95f;
constexpr int kMaxFlashesPerSec = 3;        // beat pulses per second, at most
constexpr int64_t kMinPulseGapMs = (1000 + kMaxFlashesPerSec - 1) / kMaxFlashesPerSec;   // 334
constexpr float kPulseValue = 0.18f;        // extra value on a beat (still <= kMaxValue)
constexpr float kPulseAttackMs = 60.0f;     // a pulse rises (eased), never switches on
constexpr float kPulseDecayMs = 220.0f;     // e-folding
constexpr float kBeatHueStep = 0.25f;       // bold 90 degree hue jump per pulse
constexpr float kHueEaseMs = 150.0f;        // the jump is eased in
constexpr float kHueOffset = 0.875f;        // columns use phase .. phase + 0.75
constexpr float kMaxLumaStepPerSec = 0.55f; // relative luminance slew limit
constexpr float kRedMaxRatio = 0.75f;       // linear R/(R+G+B), WCAG red = >= 0.8
constexpr float kColorEaseMs = 90.0f;       // color smoothing
constexpr int kPeriodMs = 120;              // background redraw period (~8 fps)

struct Rgbf { float r, g, b; };

inline float Lin(float c) {                 // sRGB 0..1 -> linear
    c = std::min(1.0f, std::max(0.0f, c));
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float Luma(Rgbf c) { return 0.2126f * Lin(c.r) + 0.7152f * Lin(c.g) + 0.0722f * Lin(c.b); }
inline float RedRatio(Rgbf c) {
    float r = Lin(c.r), s = r + Lin(c.g) + Lin(c.b);
    return s < 1e-4f ? 0.0f : r / s;
}
inline Rgbf Mix(Rgbf a, Rgbf b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
inline Rgbf Hsv(float h, float s, float v) {
    h = h - std::floor(h);
    auto ch = [](float x) { return std::min(1.0f, std::max(0.0f, x)); };
    float r = ch(std::fabs(h * 6 - 3) - 1), g = ch(2 - std::fabs(h * 6 - 2)), b = ch(2 - std::fabs(h * 6 - 4));
    return {v * (1 - s + s * r), v * (1 - s + s * g), v * (1 - s + s * b)};
}
// Hsv with the saturation lowered just enough to stay below kRedMaxRatio.
inline Rgbf SafeHsv(float h, float s, float v) {
    Rgbf c = Hsv(h, s, v);
    if (RedRatio(c) < kRedMaxRatio) return c;
    float lo = 0.0f, hi = s;                // RedRatio grows with s near red
    for (int i = 0; i < 14; ++i) {
        float m = 0.5f * (lo + hi);
        if (RedRatio(Hsv(h, m, v)) < kRedMaxRatio) lo = m; else hi = m;
    }
    return Hsv(h, lo, v);
}
inline uint32_t ToRgb(Rgbf c) {
    auto q = [](float x) { return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, x)) * 255.0f); };
    return (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}
inline uint16_t To565(uint32_t rgb) {
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
inline Rgbf From565(uint16_t c) {           // what the panel shows
    return {((c >> 11) & 31) / 31.0f, ((c >> 5) & 63) / 63.0f, (c & 31) / 31.0f};
}

struct Background {
    Rgbf cur{1, 1, 1};                      // white = no tint (multiply identity)
    float hue_off = 0.0f;                   // accumulated beat jumps
    float jump = 0.0f;                      // jump still to ease in
    float pulse = 0.0f;
    bool rising = false;
    int64_t last_ms = -1;
    int64_t last_pulse_ms = -1000000;
    int pulses = 0;

    // Fully back to white: the tint object can be hidden.
    bool Idle() const { return cur.r >= 0.999f && cur.g >= 0.999f && cur.b >= 0.999f; }
    void Reset() { *this = Background(); }

    // want: rave on and the DJ screen showing. phase: the EQ palette phase (0..1).
    // energy: mean bar level 0..1. beat: a bass beat since the last step.
    // Returns true if a pulse started.
    bool Step(int64_t now_ms, bool want, float phase, float energy, bool beat) {
        float dt = last_ms < 0 ? 0.0f : (float)std::min<int64_t>(500, std::max<int64_t>(0, now_ms - last_ms));
        last_ms = now_ms;
        bool started = false;
        if (want && beat && now_ms - last_pulse_ms >= kMinPulseGapMs) {
            last_pulse_ms = now_ms;
            rising = true;
            jump += kBeatHueStep;
            ++pulses;
            started = true;
        }
        if (jump > 0.0f) {
            float a = std::min(jump, jump * std::min(1.0f, dt / kHueEaseMs) + 0.002f);
            hue_off = hue_off + a;
            hue_off -= std::floor(hue_off);
            jump -= a;
        }
        if (rising) {
            pulse += (1.0f - pulse) * std::min(1.0f, dt / kPulseAttackMs);
            if (pulse >= 0.95f) { pulse = 1.0f; rising = false; }
        } else {
            pulse *= std::exp(-dt / kPulseDecayMs);
            if (pulse < 0.01f) pulse = 0.0f;
        }
        Rgbf target{1, 1, 1};
        if (want) {
            energy = std::min(1.0f, std::max(0.0f, energy));
            float v = kMinValue + (kMaxValue - kMinValue - kPulseValue) * energy + kPulseValue * pulse;
            v = std::min(kMaxValue, std::max(0.0f, v));
            target = SafeHsv(phase + kHueOffset + hue_off, kSaturation, v);
        }
        float a = dt <= 0.0f ? 0.0f : 1.0f - std::exp(-dt / kColorEaseMs);
        const float y0 = Luma(cur), r0 = RedRatio(cur), max_dy = kMaxLumaStepPerSec * dt / 1000.0f;
        auto ok = [&](float t) {
            Rgbf c = Mix(cur, target, t);
            if (std::fabs(Luma(c) - y0) > max_dy) return false;
            float rr = RedRatio(c);
            return rr < kRedMaxRatio || rr <= r0;
        };
        if (!ok(a)) {                       // largest step that keeps both limits
            float lo = 0.0f, hi = a;
            for (int i = 0; i < 16; ++i) {
                float m = 0.5f * (lo + hi);
                if (ok(m)) lo = m; else hi = m;
            }
            a = lo;
        }
        cur = Mix(cur, target, a);
        if (!want && cur.r > 0.995f && cur.g > 0.995f && cur.b > 0.995f &&
            std::fabs(1.0f - Luma(cur)) <= max_dy) cur = {1, 1, 1};
        return started;
    }
    uint32_t Rgb() const { return ToRgb(cur); }
};

}  // namespace hg_rave
