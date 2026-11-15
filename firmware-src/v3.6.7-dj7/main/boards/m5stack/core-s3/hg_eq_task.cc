// v3.6.7-dj2: DJ equalizer worker. v3.6.7-dj3: + beat times, DJ light sync task.
//
// hg_meter::Publish() (audio output task) hands each chunk to Tap(), which
// returns at once unless the DJ screen shows; then it only copies the chunk
// into a PSRAM slot and wakes this task (same two-slot scheme as hg_lip.cc).
// The task (priority 1, below hg_lip, core 1, PSRAM stack) runs the 13-band
// analyzer (audio/hg_eq.h) on each 20 ms window and stores the levels next to
// the chunk's sequence number. The display timer reads the window playing now
// (same clock as hg_meter::Sample). If the task falls behind, a chunk is
// dropped and the bars hold their last frame; playback is never waited on.
#include "hg_eq_task.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>
#include <new>

#include "audio/hg_audio_meter.h"

#define TAG "hg_eq"

namespace hg_eq_task {
namespace {

constexpr int kMaxRate = 48000;
constexpr size_t kMaxChunk = (size_t)hg_meter::kMaxWins * (kMaxRate * hg_meter::kWinMs / 1000);

struct Slot {
    int16_t pcm[kMaxChunk];
    size_t n;
    int rate;
    uint32_t seq;
    int64_t start_us;   // when this chunk starts playing (hg_meter clock)
};

Slot* g_slots = nullptr;                 // 2 slots in PSRAM
std::atomic<int> g_ready{-1};
std::atomic<int> g_busy{-1};
std::atomic<bool> g_active{false};
std::atomic<bool> g_reset{false};
std::atomic<uint32_t> g_drops{0};
TaskHandle_t g_task = nullptr;
hg_eq::Analyzer* g_an = nullptr;

// Results: levels per 20 ms window of chunk `g_done_seq`.
std::atomic<uint32_t> g_done_seq{0};
std::atomic<int> g_done_wins{0};
std::atomic<uint8_t> g_lv[hg_meter::kMaxWins][hg_eq::kBands];

// v3.6.7-dj3: play times of the last 8 beats (a 320 ms chunk holds at most 2).
constexpr int kBeatRing = 8;
std::atomic<int64_t> g_beat_us[kBeatRing];
std::atomic<uint32_t> g_beat_n{0};

// v3.6.7-dj3: LED frames for the hg_djled task.
LedSink g_sink;
std::atomic<bool> g_sink_set{false};
std::atomic<uint32_t> g_led[hg_eq::kLedCount];
std::atomic<uint32_t> g_led_seq{0};
TaskHandle_t g_led_task = nullptr;

void Tap(const int16_t* pcm, size_t n, int rate, uint32_t seq) {
    if (!g_active.load(std::memory_order_relaxed) || g_task == nullptr) return;
    if (n == 0 || rate <= 0 || rate > kMaxRate) return;
    if (n > kMaxChunk) n = kMaxChunk;
    static int last = 1;
    int slot = last ^ 1;
    if (g_busy.load() == slot) { g_drops.fetch_add(1); return; }
    if (g_ready.load() >= 0) g_drops.fetch_add(1);
    last = slot;
    memcpy(g_slots[slot].pcm, pcm, n * sizeof(int16_t));
    g_slots[slot].n = n;
    g_slots[slot].rate = rate;
    g_slots[slot].seq = seq;
    g_slots[slot].start_us = hg_meter::Get().start_us.load();
    g_ready.store(slot);
    xTaskNotifyGive(g_task);
}

void Task(void*) {
    int64_t stat_t0 = esp_timer_get_time();
    int64_t sum_us = 0, max_us = 0;
    uint32_t count = 0;
    uint8_t out[hg_eq::kBands];
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int slot = g_ready.exchange(-1);
        if (slot < 0) continue;
        g_busy.store(slot);
        const Slot& s = g_slots[slot];
        if (s.rate != g_an->rate()) g_an->Init(s.rate);
        if (g_reset.exchange(false)) g_an->Reset();
        const size_t win = (size_t)s.rate * hg_meter::kWinMs / 1000;
        int idx = 0;
        for (size_t off = 0; off < s.n && idx < hg_meter::kMaxWins; off += win, ++idx) {
            size_t len = (off + win < s.n) ? win : s.n - off;
            int64_t t0 = esp_timer_get_time();
            if (g_an->Push(s.pcm + off, len, out)) {
                uint32_t b = g_beat_n.load();
                g_beat_us[b % kBeatRing].store(s.start_us + (int64_t)idx * hg_meter::kWinMs * 1000);
                g_beat_n.store(b + 1);
            }
            int64_t dt = esp_timer_get_time() - t0;
            sum_us += dt; if (dt > max_us) max_us = dt; ++count;
            for (int k = 0; k < hg_eq::kBands; ++k) g_lv[idx][k].store(out[k], std::memory_order_relaxed);
        }
        g_done_wins.store(idx);
        g_done_seq.store(s.seq);
        g_busy.store(-1);
        int64_t now = esp_timer_get_time();
        if (now - stat_t0 > 10 * 1000 * 1000 && count > 0) {
            ESP_LOGI(TAG, "eq analysis: %lu windows, avg %lld us, max %lld us, drops %lu",
                     (unsigned long)count, (long long)(sum_us / count), (long long)max_us,
                     (unsigned long)g_drops.load());
            stat_t0 = now; sum_us = 0; max_us = 0; count = 0;
        }
    }
}

void LedTask(void*) {
    uint32_t done = 0;
    uint32_t last[hg_eq::kLedCount] = {};
    uint32_t frame[hg_eq::kLedCount];
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        uint32_t q = g_led_seq.load();
        if (q == done || !g_active.load() || !g_sink_set.load()) continue;
        done = q;
        bool same = true;
        for (int i = 0; i < hg_eq::kLedCount; ++i) {
            frame[i] = g_led[i].load(std::memory_order_relaxed);
            same = same && frame[i] == last[i];
        }
        if (!same) {
            g_sink.frame(g_sink.ctx, frame);
            std::memcpy(last, frame, sizeof(last));
        }
        vTaskDelay(pdMS_TO_TICKS(kLedPeriodMs));   // rate limit: <= 20 writes/s
    }
}

}  // namespace

void SetActive(bool on) {
    if (on == g_active.load()) return;
    if (on) g_reset.store(true);
    g_active.store(on);
    if (g_sink_set.load() && g_sink.sync) g_sink.sync(g_sink.ctx, on);
}

bool BeatSince(int64_t* last_us) {
    const int64_t now = esp_timer_get_time();
    bool hit = false;
    int64_t newest = *last_us;
    for (int i = 0; i < kBeatRing; ++i) {
        int64_t t = g_beat_us[i].load();
        if (t > *last_us && t <= now) { hit = true; if (t > newest) newest = t; }
    }
    *last_us = newest;
    return hit;
}

void SetLedSink(const LedSink& sink) {
    if (sink.frame == nullptr) return;
    g_sink = sink;
    g_sink_set.store(true);
}

void PublishLeds(const uint32_t rgb12[hg_eq::kLedCount]) {
    if (g_led_task == nullptr || !g_sink_set.load()) return;
    for (int i = 0; i < hg_eq::kLedCount; ++i) g_led[i].store(rgb12[i], std::memory_order_relaxed);
    g_led_seq.fetch_add(1);
    xTaskNotifyGive(g_led_task);
}

Result Sample(uint8_t out[hg_eq::kBands], int stale_ms) {
    auto& m = hg_meter::Get();
    int64_t t0 = m.start_us.load();
    int n = m.wins.load();
    uint32_t q = m.seq.load();
    if (t0 == 0 || n == 0) return kSilent;
    int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
    if (dt_ms > (int64_t)n * hg_meter::kWinMs + stale_ms) return kSilent;
    if (g_done_seq.load() != q) return kPending;
    int idx = (int)(dt_ms / hg_meter::kWinMs);
    int done = g_done_wins.load();
    if (idx >= done) idx = done - 1;
    if (idx < 0) idx = 0;
    for (int k = 0; k < hg_eq::kBands; ++k) out[k] = g_lv[idx][k].load(std::memory_order_relaxed);
    return kLive;
}

void Start() {
    if (g_task != nullptr) return;
    g_slots = static_cast<Slot*>(heap_caps_calloc(2, sizeof(Slot), MALLOC_CAP_SPIRAM));
    void* mem = heap_caps_calloc(1, sizeof(hg_eq::Analyzer), MALLOC_CAP_SPIRAM);
    if (g_slots == nullptr || mem == nullptr) {
        ESP_LOGE(TAG, "no PSRAM for the DJ equalizer; bars stay flat");
        if (g_slots) heap_caps_free(g_slots);
        if (mem) heap_caps_free(mem);
        g_slots = nullptr;
        return;
    }
    g_an = new (mem) hg_eq::Analyzer();
    g_an->Init(24000);
    // Lowest app priority, core 1, PSRAM stack (math on PSRAM buffers + logs only).
    if (xTaskCreatePinnedToCoreWithCaps(Task, "hg_eq", 6144, nullptr, 1, &g_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "task start failed; bars stay flat");
        g_task = nullptr;
        return;
    }
    hg_meter::Get().tap2.store(&Tap);
    // v3.6.7-dj3: DJ light sync writer (I2C to the PY32 LED driver), lowest priority.
    if (xTaskCreatePinnedToCoreWithCaps(LedTask, "hg_djled", 3072, nullptr, 1, &g_led_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "LED task start failed; DJ lights not synced");
        g_led_task = nullptr;
    }
    ESP_LOGI(TAG, "DJ equalizer ready (%d bands, %d-point FFT, %u bytes PSRAM)", hg_eq::kBands,
             hg_eq::kFftN, (unsigned)(2 * sizeof(Slot) + sizeof(hg_eq::Analyzer)));
}

}  // namespace hg_eq_task
