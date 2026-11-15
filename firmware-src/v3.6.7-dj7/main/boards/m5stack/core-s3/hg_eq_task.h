// v3.6.7-dj2: DJ equalizer worker (13 bands from the PCM going to the speaker).
#pragma once
#include <cstdint>
#include "audio/hg_eq.h"

namespace hg_eq_task {
void Start();               // once at boot (PSRAM buffers + a low-priority task)
void SetActive(bool on);    // only analyzes while the DJ screen shows
// Levels (0..255) of the 20 ms window playing now.
//   kLive    out[] filled
//   kPending audio is playing but this chunk is not analyzed yet (keep the last frame)
//   kSilent  nothing played recently (bars fall to 0)
enum Result { kSilent = 0, kLive = 1, kPending = 2 };
Result Sample(uint8_t out[hg_eq::kBands], int stale_ms = 150);

// v3.6.7-dj3: bass beats. True if a beat started playing in (*last_us, now];
// *last_us moves to that beat. Start with *last_us = now.
bool BeatSince(int64_t* last_us);

// v3.6.7-dj3: DJ light sync. The board sets a sink (her LED driver). While the
// DJ screen shows, the display hands a 12-LED frame (0xRRGGBB) to PublishLeds()
// every frame; the hg_djled task (priority 1) writes at most one frame per
// kLedPeriodMs. sync(true/false) is called when the DJ screen turns on / off.
constexpr int kLedPeriodMs = 50;     // 20 Hz
struct LedSink {
    void (*frame)(void* ctx, const uint32_t* rgb12) = nullptr;
    void (*sync)(void* ctx, bool on) = nullptr;
    void* ctx = nullptr;
};
void SetLedSink(const LedSink& sink);
void PublishLeds(const uint32_t rgb12[hg_eq::kLedCount]);
}  // namespace hg_eq_task
