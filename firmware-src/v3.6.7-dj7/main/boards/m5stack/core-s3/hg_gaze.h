// headphone-girl v3.6 "Follow faces": turn face boxes from the on-device
// detector (esp-dl, see hg_face_dl.cc) into gentle head moves. Pure code (no
// device headers) so the host unit test can include it.
#pragma once
#include <cmath>
#include <cstdint>

namespace hg_gaze {

struct Box {                   // detector output, pixels in the camera frame
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float score = 0;
};

struct Face {
    bool found = false;
    float cx = 0, cy = 0;      // -1..1, +x = right side of the image, +y = down
    int cells = 0;             // box area in pixels (bigger = closer)
    float score = 0;           // v3.6.2: detector score of that box
    Box box;                   // v3.6.2: the box itself (debug line)
};

// v3.6.2: the model's own cut-off (MSR/MNP default 0.5); 0.6 dropped real faces.
constexpr float kMinScore = 0.5f;
// v3.6.2: at or above this, one sighting is enough to start moving.
constexpr float kConfidentScore = 0.7f;
inline int SightingsNeeded(float score) { return score >= kConfidentScore ? 1 : 2; }
constexpr int kMinSidePx = 14;         // smaller boxes are mostly noise at QVGA

// Biggest confident face. Aims a little above the box centre (the eyes).
inline Face PickFace(const Box* boxes, int n, int w, int h, float min_score = kMinScore) {
    Face best;
    if (boxes == nullptr || w <= 0 || h <= 0) return best;
    for (int i = 0; i < n; ++i) {
        const Box& b = boxes[i];
        int bw = b.x1 - b.x0, bh = b.y1 - b.y0;
        if (b.score < min_score || bw < kMinSidePx || bh < kMinSidePx) continue;
        if (bw > w * 9 / 10 && bh > h * 9 / 10) continue;   // whole frame: not a face
        int area = bw * bh;
        if (area <= best.cells) continue;
        best.found = true;
        best.cells = area;
        best.score = b.score;
        best.box = b;
        best.cx = (b.x0 + b.x1) * 0.5f / w * 2.0f - 1.0f;
        best.cy = (b.y0 + 0.42f * bh) / h * 2.0f - 1.0f;
    }
    if (best.cx > 1) best.cx = 1;
    if (best.cx < -1) best.cx = -1;
    if (best.cy > 1) best.cy = 1;
    if (best.cy < -1) best.cy = -1;
    return best;
}

// One step toward the face. sign_x / sign_y are +1 or -1 (learned).
// v3.6.2: faster. Moves ~75 % of the way per step (was 55 %), caps 15 deg yaw /
// 9 deg pitch (were 10 / 6), slightly smaller dead zone (0.10 / 0.13).
constexpr float kDeadX = 0.10f, kDeadY = 0.13f;
constexpr float kGainX = 0.75f, kGainY = 0.70f;
constexpr float kMaxStepYaw = 15.0f, kMaxStepPitch = 9.0f;
struct Step { bool move = false; float dyaw = 0, dpitch = 0; };
inline Step PlanStep(const Face& f, int sign_x, int sign_y) {
    Step s;
    if (!f.found) return s;
    float ex = std::fabs(f.cx) < kDeadX ? 0.0f : f.cx;
    float ey = std::fabs(f.cy) < kDeadY ? 0.0f : f.cy;
    if (ex == 0.0f && ey == 0.0f) return s;
    // about 60 x 45 degrees field of view (half: 30 x 22)
    s.dyaw = sign_x * ex * 30.0f * kGainX;
    s.dpitch = sign_y * -ey * 22.0f * kGainY;      // face above centre -> look up
    s.dyaw = s.dyaw > kMaxStepYaw ? kMaxStepYaw : (s.dyaw < -kMaxStepYaw ? -kMaxStepYaw : s.dyaw);
    s.dpitch = s.dpitch > kMaxStepPitch ? kMaxStepPitch
                                         : (s.dpitch < -kMaxStepPitch ? -kMaxStepPitch : s.dpitch);
    s.move = std::fabs(s.dyaw) >= 2.0f || std::fabs(s.dpitch) >= 2.0f;
    return s;
}

// v3.6.2: move time scales with the step: about 18 ms per degree, 180..420 ms.
inline int MoveMs(float dyaw, float dpitch) {
    float d = std::fabs(dyaw) > std::fabs(dpitch) ? std::fabs(dyaw) : std::fabs(dpitch);
    int ms = (int)(d * 18.0f);
    return ms < 180 ? 180 : (ms > 420 ? 420 : ms);
}
constexpr int kSettleMs = 200;      // let the picture settle after a move (was 450)
constexpr int kLoopMs = 100;        // pause between checks (was 250)

// Where follow faces may point the head. Built around the saved home pitch
// (level = 3 by default, which is also the lowest angle): she looks straight
// at a face at her height and up (to about home + 37) at someone standing.
// If home was raised in Settings she may also dip below it, never under 3.
struct Window { float yaw_lo, yaw_hi, pitch_lo, pitch_hi; };
constexpr float kPitchFloor = 3.0f;   // CoreS3Servo kPitchMin
inline Window GazeWindow(float home_yaw, float home_pitch) {
    Window w;
    w.yaw_lo = home_yaw - 45.0f;
    w.yaw_hi = home_yaw + 45.0f;
    if (w.yaw_lo < -60.0f) w.yaw_lo = -60.0f;
    if (w.yaw_hi > 60.0f) w.yaw_hi = 60.0f;
    w.pitch_lo = home_pitch - 18.0f;
    w.pitch_hi = home_pitch + 37.0f;
    if (w.pitch_lo < kPitchFloor) w.pitch_lo = kPitchFloor;   // servo floor = level
    if (w.pitch_hi > 75.0f) w.pitch_hi = 75.0f;
    if (w.pitch_lo > home_pitch) w.pitch_lo = home_pitch;
    if (w.pitch_hi < home_pitch) w.pitch_hi = home_pitch;
    return w;
}

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// No face for this long after a follow move: drift back to home (level).
constexpr int kReturnHomeMs = 6000;
inline bool ShouldReturnHome(bool moved_away, int64_t last_seen_ms, int64_t now_ms) {
    return moved_away && last_seen_ms > 0 && now_ms - last_seen_ms >= kReturnHomeMs;
}

// Learns whether a move toward the face made the error smaller. Three bad
// moves in a row (face drifts further off in the same direction) flip it.
// A clearly bad move (offset grew by more than 0.18, about 5 deg) counts
// double, so a mirrored camera is fixed on the second try, before the face
// is pushed out of view.
struct SignLearner {
    int sign = 1;
    int wrong = 0;
    // before/after: face offset on this axis around a move of at least 3 deg.
    // Returns true if the sign flipped.
    bool Update(float before, float after) {
        if (std::fabs(before) < 0.2f) return false;
        bool same_side = (before > 0) == (after > 0);
        if (same_side && std::fabs(after) > std::fabs(before) + 0.08f) {
            wrong += (std::fabs(after) > std::fabs(before) + 0.18f) ? 2 : 1;
            if (wrong >= 3) { sign = -sign; wrong = 0; return true; }
        } else if (std::fabs(after) < std::fabs(before) - 0.05f || !same_side) {
            wrong = 0;
        }
        return false;
    }
    // v3.6.2: the face vanished right after a move on this axis. If it was
    // already well off centre (|before| >= 0.35), a step the wrong way is the
    // likely reason (it pushes the face out of the picture), so count one
    // strike. A right-way step brings it to the middle, where it stays visible.
    bool Lost(float before) {
        if (std::fabs(before) < 0.35f) return false;
        if (++wrong >= 3) { sign = -sign; wrong = 0; return true; }
        return false;
    }
};

}  // namespace hg_gaze
