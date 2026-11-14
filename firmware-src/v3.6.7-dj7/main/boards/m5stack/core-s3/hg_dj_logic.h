// v3.6.7-dj: pure logic for DJ mode (no ESP-IDF, no LVGL), unit-tested on the
// host (build/firmware/v3.6.7-dj/host-tests/test_dj_logic.cc).
//   hg_gesture  screen tap vs swipe vs hold (v3.6.7-dj2: DJ buttons removed)
//   hg_vu       VU meter envelope -> lit segments, VU modes and palettes
//               (bars across the top of the screen)
//   hg_djmove   the looping DJ groove pose, always inside the head caps
#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace hg_gesture {
// A tap may wobble up to kTapSlopPx. A swipe must travel at least kSwipeMinPx,
// mostly in one direction (main axis >= 1.5 x the other), within kSwipeMaxMs.
// Left / right = songs. Up / down are recognized (so never a tap) but do
// nothing since v3.6.7-dj5: rave mode is voice only.
// Anything in between is ignored, so a sloppy swipe never turns into
// tap-to-interrupt. v3.6.7-dj5: looser (was 60 px, 2:1, 800 ms; now 50 px, 1.5:1, 1200 ms).
constexpr int kTapSlopPx = 24;
constexpr int kSwipeMinPx = 50;
constexpr int64_t kSwipeMaxMs = 1200;
constexpr int64_t kHoldMs = 1500;   // Settings (unchanged from v3)

enum Kind { kNone = 0, kTap = 1, kSwipeNext = 2, kSwipePrev = 3, kHold = 4, kSwipeUp = 5, kSwipeDown = 6 };

inline const char* KindName(Kind k) {
    switch (k) {
        case kTap: return "tap";
        case kSwipeNext: return "swipe L";
        case kSwipePrev: return "swipe R";
        case kSwipeUp: return "swipe U";
        case kSwipeDown: return "swipe D";
        case kHold: return "hold";
        default: return "none";
    }
}

// dx, dy: last point minus touch-down point. max_dist: largest distance
// (max of |dx|,|dy|) seen while the finger was down. dur_ms: touch time.
inline Kind Classify(int dx, int dy, int max_dist, int64_t dur_ms) {
    if (max_dist <= kTapSlopPx) return dur_ms >= kHoldMs ? kHold : kTap;
    int ax = std::abs(dx), ay = std::abs(dy);
    if (dur_ms > kSwipeMaxMs) return kNone;
    if (ax >= kSwipeMinPx && 2 * ax >= 3 * ay)
        return dx < 0 ? kSwipeNext : kSwipePrev;   // finger moves left = next
    if (ay >= kSwipeMinPx && 2 * ay >= 3 * ax)
        return dy < 0 ? kSwipeUp : kSwipeDown;     // screen y grows downward
    return kNone;
}

// Hold-for-Settings only counts while the finger stays put.
inline bool HoldQualifies(int max_dist, int64_t held_ms) {
    return max_dist <= kTapSlopPx && held_ms >= kHoldMs;
}

// ── v3.6.7-dj5: touch tracker ──
// Nothing ever happens on touch-down: a gesture is decided only after the
// finger is gone. Why: the FT6336 is read over the shared I2C bus every 20 ms;
// on the DJ screen the bus also carries the LED frames (dj3+), the codec and
// the PY32, and a read that fails, or a frame where the panel briefly reports
// no finger, used to count as "finger lifted" at once. A swipe then ended
// after its first 20-40 ms with almost no movement = a TAP = tap-to-interrupt,
// which stopped the song before the swipe was ever seen.
//   - A failed or invalid read is "no news": the touch state is kept.
//   - "No finger" must last kReleaseMs before it counts as a lift; a finger
//     seen again within that time continues the same touch.
//   - If reads keep failing for kStaleMs while down, the touch ends with the
//     last good point.
//   - On the DJ screen a tap (or a move too short to call) also waits
//     kDjTapWaitMs: a touch that starts in that time continues the same gesture (a swipe broken by a longer dropout
//     is still a swipe, never an interrupt).
//   - On the DJ screen any touch that starts within kDjAfterGestureMs
//     after a gesture ended is the end of that one (finger lifting): no tap,
//     no second skip.
constexpr int64_t kReleaseMs = 60;
constexpr int64_t kStaleMs = 300;
constexpr int64_t kDjTapWaitMs = 200;
constexpr int64_t kDjAfterGestureMs = 300;   // DJ: a touch this soon after a gesture = its tail
constexpr int kScreenW = 320, kScreenH = 240;

struct Gesture {
    Kind kind = kNone;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int max_dist = 0;
    int64_t dur_ms = 0;
    int samples = 0;   // good finger samples
    int drops = 0;     // failed / invalid reads and brief no-finger frames while down
    int joins = 0;     // DJ: touches joined after a tap wait
    bool dj = false;
    bool tail = false; // DJ: ignored as the tail of the swipe before
};

class Tracker {
public:
    enum State { kIdle, kDown, kLifting, kTapWait };
    // One poll. ok = the I2C read worked. num = fingers reported, x / y = first
    // point. dj = the DJ screen is showing. Returns true with *out when a
    // gesture is decided (always after the finger has gone).
    bool Poll(int64_t now_ms, bool ok, int num, int x, int y, bool dj, Gesture* out) {
        const bool valid = ok && num >= 0 && num <= 2 &&
                           (num == 0 || (x >= 0 && x < kScreenW + 40 && y >= 0 && y < kScreenH + 40));
        if (!valid) {
            if (state_ == kDown || state_ == kLifting) {
                ++g_.drops;
                if (now_ms - last_good_ms_ >= kStaleMs) return Decide(last_good_ms_, dj, now_ms, out);
            }
            if (state_ == kTapWait && now_ms >= tap_deadline_) return Emit(out);
            return false;
        }
        if (num > 0) {
            x = std::min(x, kScreenW - 1);
            y = std::min(y, kScreenH - 1);
            switch (state_) {
                case kIdle:
                    g_ = Gesture();
                    g_.x0 = g_.x1 = x;
                    g_.y0 = g_.y1 = y;
                    t0_ = now_ms;
                    state_ = kDown;
                    break;
                case kLifting: ++g_.drops; state_ = kDown; break;   // a dropout, same touch
                case kTapWait: ++g_.joins; state_ = kDown; break;   // DJ: same gesture goes on
                case kDown: break;
            }
            g_.x1 = x;
            g_.y1 = y;
            g_.max_dist = std::max(g_.max_dist, std::max(std::abs(x - g_.x0), std::abs(y - g_.y0)));
            ++g_.samples;
            last_good_ms_ = now_ms;
            return false;
        }
        // num == 0: no finger
        switch (state_) {
            case kDown: state_ = kLifting; up_ms_ = last_good_ms_; return false;
            case kLifting:
                if (now_ms - up_ms_ >= kReleaseMs) return Decide(up_ms_, dj, now_ms, out);
                return false;
            case kTapWait:
                if (now_ms >= tap_deadline_) return Emit(out);
                return false;
            case kIdle: return false;
        }
        return false;
    }
    State state() const { return state_; }
    bool FingerDown() const { return state_ == kDown || state_ == kLifting; }
    // While down: has the finger been still for kHoldMs (Settings)?
    bool HoldReady(int64_t now_ms) const {
        return FingerDown() && HoldQualifies(g_.max_dist, now_ms - t0_);
    }

private:
    bool Decide(int64_t end_ms, bool dj, int64_t now_ms, Gesture* out) {
        g_.dur_ms = std::max<int64_t>(0, end_ms - t0_);
        g_.kind = Classify(g_.x1 - g_.x0, g_.y1 - g_.y0, g_.max_dist, g_.dur_ms);
        g_.dj = dj;
        if (dj && g_.joins == 0 && t0_ - last_gesture_end_ms_ < kDjAfterGestureMs) {
            g_.kind = kNone;   // the tail of the gesture just before (never a 2nd skip or a tap)
            g_.tail = true;
            return Emit(out);
        }
        if ((g_.kind == kTap || g_.kind == kNone) && dj && g_.joins == 0) {
            state_ = kTapWait;   // a tap, or a move too short to call: wait for more
            tap_deadline_ = now_ms + kDjTapWaitMs;
            return false;
        }
        return Emit(out);
    }
    bool Emit(Gesture* out) {
        if (state_ == kTapWait) {   // re-classify a joined gesture is done in Decide; a plain wait = tap
            g_.kind = Classify(g_.x1 - g_.x0, g_.y1 - g_.y0, g_.max_dist, g_.dur_ms);
        }
        state_ = kIdle;
        if (g_.kind != kTap) last_gesture_end_ms_ = t0_ + g_.dur_ms;
        if (out) *out = g_;
        return true;
    }
    State state_ = kIdle;
    Gesture g_;
    int64_t t0_ = 0, last_good_ms_ = 0, up_ms_ = 0, tap_deadline_ = 0;
    int64_t last_gesture_end_ms_ = -1000000;
};

}  // namespace hg_gesture

namespace hg_vu {
constexpr int kSegments = 12;
// One meter: fast attack, slower fall, adaptive peak so quiet and loud songs
// both fill the meter, plus a peak-hold marker that falls after ~0.6 s.
struct Meter {
    float env = 0.0f;
    float peak = 1500.0f;
    int hold_seg = 0;
    int hold_ticks = 0;
    // x: loudness (RMS of the 20 ms window playing now), 0 when silent.
    // Returns lit segments 0..kSegments.
    int Step(float x) {
        env = x > env ? 0.6f * x + 0.4f * env : 0.8f * env + 0.2f * x;
        peak *= 0.996f;
        if (env > peak) peak = env;
        if (peak < 1200.0f) peak = 1200.0f;
        float level = env / peak;
        if (level > 1.0f) level = 1.0f;
        if (level < 0.0f) level = 0.0f;
        int lit = (int)std::lround(std::pow(level, 0.8f) * kSegments);
        if (lit > kSegments) lit = kSegments;
        if (lit >= hold_seg) { hold_seg = lit; hold_ticks = 15; }
        else if (hold_ticks > 0) { --hold_ticks; }
        else if (hold_seg > 0) { --hold_seg; }
        return lit;
    }
};
// The right meter is "brightness": loudness weighted by the zero-crossing rate
// (more treble = more crossings). zcr is crossings per 1000 samples.
inline float Brightness(float rms, int zcr) {
    float t = zcr / 80.0f;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    return rms * (0.7f + 0.6f * t);
}

// VU modes from the set list cue {"action":"vu","value":...} / tool self.dj.vu.
enum class Palette { kClassic, kPurple, kBlue, kGold, kWarm, kRainbow };
enum class ModeKind { kBad, kColor, kOn, kOff, kPulse };
struct Mode { ModeKind kind; Palette palette; };
inline Mode ParseMode(const std::string& in) {
    std::string m;
    for (char c : in) if (c != ' ' && c != '_' && c != '-') m += (char)std::tolower((unsigned char)c);
    if (m == "on" || m == "show") return {ModeKind::kOn, Palette::kClassic};
    if (m == "off" || m == "hide") return {ModeKind::kOff, Palette::kClassic};
    if (m == "pulse") return {ModeKind::kPulse, Palette::kClassic};
    if (m == "classic" || m == "default" || m == "normal" || m == "green") return {ModeKind::kColor, Palette::kClassic};
    if (m == "purple" || m == "violet") return {ModeKind::kColor, Palette::kPurple};
    if (m == "blue") return {ModeKind::kColor, Palette::kBlue};
    if (m == "gold" || m == "yellow") return {ModeKind::kColor, Palette::kGold};
    if (m == "warm" || m == "orange" || m == "warmwhite") return {ModeKind::kColor, Palette::kWarm};
    if (m == "rainbow") return {ModeKind::kColor, Palette::kRainbow};
    return {ModeKind::kBad, Palette::kClassic};
}
inline uint32_t Mix(uint32_t a, uint32_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float x = ((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t;
        out |= (uint32_t)std::lround(x) << sh;
    }
    return out;
}
inline uint32_t Hue(float h) {     // h 0..1 -> saturated RGB
    h = h - std::floor(h);
    float r = std::fabs(h * 6 - 3) - 1, g = 2 - std::fabs(h * 6 - 2), b = 2 - std::fabs(h * 6 - 4);
    auto c = [](float v) { return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}
// Color of segment i (0 = nearest the center, n-1 = the outer end).
inline uint32_t SegColor(Palette p, int i, int n) {
    float t = n > 1 ? (float)i / (n - 1) : 0.0f;
    switch (p) {
        case Palette::kPurple: return Mix(0x7C4DFF, 0xFF40FF, t);
        case Palette::kBlue: return Mix(0x2962FF, 0x00E5FF, t);
        case Palette::kGold: return Mix(0xFF8F00, 0xFFF59D, t);
        case Palette::kWarm: return Mix(0xFF5A1F, 0xFFD180, t);
        case Palette::kRainbow: return Hue(0.8f * t);
        case Palette::kClassic:
        default: return i < n * 7 / 12 ? 0x00E676 : (i < n * 10 / 12 ? 0xFFEA00 : 0xFF1744);
    }
}
// A "pulse": a bright wave that fills the bars and breathes at 2 Hz; returns
// the minimum lit count while it runs (0 when over).
inline int PulseLit(int64_t now_ms, int64_t until_ms, int n) {
    if (now_ms >= until_ms) return 0;
    float ph = (float)(now_ms % 500) / 500.0f;
    float v = 0.55f + 0.45f * std::sin(ph * 6.2832f);
    return std::max(1, std::min(n, (int)std::lround(v * n)));
}
// Top-of-screen layout: two bars mirrored out from the center. Left = loudness
// growing to the left, right = brightness growing to the right.
constexpr int kTopY = 3, kSegW = 10, kSegH = 10, kSegStep = 13, kCenterGap = 4;
inline int SegX(int side, int i) {   // side 0 = left, 1 = right; i 0 = nearest the center
    return side == 0 ? 160 - kCenterGap / 2 - kSegW - i * kSegStep : 160 + kCenterGap / 2 + i * kSegStep;
}
}  // namespace hg_vu

namespace hg_djmove {
// Caps for the DJ groove, inside the existing dance moves (presets go to
// yaw +-35 and pitch +20 above home; v3.5 groove to yaw +-22).
constexpr float kYawMax = 22.0f;     // degrees either side
constexpr float kPitchUpMax = 12.0f; // degrees above home (home = level, her lowest)
constexpr int kBeatMinMs = 350;      // 171 bpm
constexpr int kBeatMaxMs = 1000;     // 60 bpm
constexpr int kMoveEveryMs = 100;    // servo command rate
constexpr int kServoSpeed = 700;     // presets use 800, groove 900

struct Pose { float yaw; float dpitch; };

inline int ClampBeat(int beat_ms) {
    return beat_ms < kBeatMinMs ? kBeatMinMs : (beat_ms > kBeatMaxMs ? kBeatMaxMs : beat_ms);
}

// t_ms: time since the groove started. beat_ms: current beat estimate.
// energy: 0..1 loudness. Sway = one side per beat, bob = up between beats and
// back down on the beat, plus a slow 8 s drift so it never looks scripted.
inline Pose DjPose(int64_t t_ms, int beat_ms, float energy) {
    const float kPi = 3.14159265f;
    beat_ms = ClampBeat(beat_ms);
    if (energy < 0.0f) energy = 0.0f;
    if (energy > 1.0f) energy = 1.0f;
    if (t_ms < 0) t_ms = 0;
    float sway_ph = 2.0f * kPi * (float)(t_ms % (2 * beat_ms)) / (float)(2 * beat_ms);
    float bob_ph = kPi * (float)(t_ms % beat_ms) / (float)beat_ms;
    float drift = 4.0f * std::sin(2.0f * kPi * (float)(t_ms % 8000) / 8000.0f);
    float yaw = (10.0f + 8.0f * energy) * std::sin(sway_ph) + drift;
    float dp = 1.0f + (4.0f + 6.0f * energy) * std::sin(bob_ph);
    if (yaw > kYawMax) yaw = kYawMax;
    if (yaw < -kYawMax) yaw = -kYawMax;
    if (dp < 0.0f) dp = 0.0f;
    if (dp > kPitchUpMax) dp = kPitchUpMax;
    return Pose{yaw, dp};
}

// Beat interval estimate from onsets (EMA), ignoring gaps that are not a beat.
inline int UpdateBeat(int beat_ms, int64_t gap_ms) {
    if (gap_ms < kBeatMinMs || gap_ms > 2 * kBeatMaxMs) return ClampBeat(beat_ms);
    if (gap_ms > kBeatMaxMs) gap_ms /= 2;   // missed one beat
    return ClampBeat((int)(0.8f * beat_ms + 0.2f * (float)gap_ms));
}
}  // namespace hg_djmove
