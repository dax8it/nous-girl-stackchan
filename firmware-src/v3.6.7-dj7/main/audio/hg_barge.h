// headphone-girl v3.6.5: "Interrupt with wake word" (barge-in with "Hey Nous-Girl").
//
// Off by default (NVS "hg"/"ww_barge" = 0). When off, MultiNet is fed only while
// she is idle, exactly as v3.3..v3.6.4. When on, MultiNet is also fed while she is
// speaking, so "Hey Nous-Girl" aborts her speech and starts listening, the same
// path Jarvis already takes.
//
// v3.3 found that MultiNet fed continuously pegs core 1 (IDLE1 watchdog, AFE FEED
// ring overflow). BargeGuard below is the pure (host-testable) safety valve used
// only while speaking with the switch on:
//   - skip while a follow-faces detect step is running (camera DSP on the chip),
//   - skip while internal RAM is below a floor,
//   - every window, measure core-1 idle time and MultiNet time; if core 1 idle
//     falls below a floor or MultiNet takes too much of the window, pause
//     MultiNet for a cooldown so IDLE1 and the AFE feed can catch up.
#pragma once

#include <atomic>
#include <cstdint>

namespace hg_barge {

inline std::atomic<bool>& EnabledFlag() {
    static std::atomic<bool> f{false};
    return f;
}
// v3.6.6: barge-in with "Hey Nous-Girl" is switched off in this build. It did
// not work on the robot (no echo cancel: her own voice masks the phrase) and
// running MultiNet next to the speaker/decoder is a crash risk. With this false,
// Enabled() is constant false, the compiler drops the speaking-state branch and
// the AFE path is exactly v3.6.4. The NVS key hg/ww_barge is ignored.
inline constexpr bool kBuildAllowsBarge = false;
inline bool Enabled() { return kBuildAllowsBarge && EnabledFlag().load(std::memory_order_relaxed); }
inline void SetEnabled(bool on) { EnabledFlag().store(on, std::memory_order_relaxed); }

enum class Why : uint8_t { kOk = 0, kFollow, kRam, kCpu };

inline const char* WhyName(Why w) {
    switch (w) {
        case Why::kOk: return "ok";
        case Why::kFollow: return "follow";
        case Why::kRam: return "ram";
        case Why::kCpu: return "cpu";
    }
    return "?";
}

struct BargeGuard {
    // Tunables.
    int64_t window_ms = 500;       // measurement window
    int min_idle_pct = 15;         // core-1 idle below this -> pause
    int max_mn_pct = 60;           // MultiNet above this share of a window -> pause
    int64_t cooldown_ms = 2000;    // pause length
    uint32_t min_sram = 32 * 1024; // internal RAM floor

    // State.
    int64_t win_start_ms = -1;
    uint32_t idle_at_start = 0;    // core-1 idle run-time counter (us, wraps at 2^32)
    int64_t mn_us = 0;             // MultiNet time charged in this window
    int64_t cooldown_until_ms = 0;

    // Last measurement + counters for the log line.
    int last_idle_pct = -1;
    int last_mn_pct = -1;
    uint32_t pauses_cpu = 0;
    uint32_t skips_follow = 0;
    uint32_t skips_ram = 0;
    uint32_t frames_fed = 0;
    int64_t total_mn_us = 0;
    int64_t total_wall_ms = 0;

    void Reset() {
        win_start_ms = -1;
        mn_us = 0;
        cooldown_until_ms = 0;
    }

    void Charge(int64_t us) {
        if (us > 0) {
            mn_us += us;
            total_mn_us += us;
        }
    }

    // Called once per AFE frame while speaking with barge-in on.
    // idle_counter_us = ulTaskGetIdleRunTimeCounterForCore(1) (us, 32-bit wrap).
    Why Allow(int64_t now_ms, uint32_t idle_counter_us, uint32_t sram_free, bool follow_busy) {
        // Window bookkeeping runs first so the CPU measurement stays continuous
        // even on frames we skip.
        if (win_start_ms < 0) {
            win_start_ms = now_ms;
            idle_at_start = idle_counter_us;
            mn_us = 0;
        } else if (now_ms - win_start_ms >= window_ms) {
            const int64_t wall_us = (now_ms - win_start_ms) * 1000;
            const uint32_t idle_us = idle_counter_us - idle_at_start;  // wrap-safe
            int idle_pct = (int)((int64_t)idle_us * 100 / wall_us);
            if (idle_pct > 100) idle_pct = 100;
            int mn_pct = (int)(mn_us * 100 / wall_us);
            if (mn_pct > 100) mn_pct = 100;
            last_idle_pct = idle_pct;
            last_mn_pct = mn_pct;
            total_wall_ms += now_ms - win_start_ms;
            if (now_ms >= cooldown_until_ms && (idle_pct < min_idle_pct || mn_pct > max_mn_pct)) {
                cooldown_until_ms = now_ms + cooldown_ms;
                ++pauses_cpu;
            }
            win_start_ms = now_ms;
            idle_at_start = idle_counter_us;
            mn_us = 0;
        }
        if (follow_busy) {
            ++skips_follow;
            return Why::kFollow;
        }
        if (sram_free < min_sram) {
            ++skips_ram;
            return Why::kRam;
        }
        if (now_ms < cooldown_until_ms) {
            return Why::kCpu;
        }
        ++frames_fed;
        return Why::kOk;
    }

    // Average MultiNet share of wall time since boot of the guard, x10 (no float printf).
    int AvgMnPctX10() const {
        return total_wall_ms > 0 ? (int)(total_mn_us / total_wall_ms) : 0;  // us/ms = x1000/1000 -> /1 -> pct*10
    }
};

}  // namespace hg_barge
