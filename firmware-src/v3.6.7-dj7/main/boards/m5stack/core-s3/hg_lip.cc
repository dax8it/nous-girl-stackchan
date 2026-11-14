// headphone-girl v3.5: vowel lip sync worker.
//
// hg_meter::Publish() (audio output task) hands each chunk to Tap(), which
// only copies it into a PSRAM slot and wakes this task. The task runs the
// avmic formant analyzer (audio/hg_vowel.h, MIT, Takao Akaki) on each 20 ms
// window and writes the result next to the loudness values. If the task falls
// behind, the older chunk is dropped; the mouth then uses the v3.4 rule.
#include "hg_lip.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>
#include <new>

#include "audio/hg_audio_meter.h"
#include "audio/hg_vowel.h"
#include "settings.h"

#define TAG "hg_lip"

namespace hg_lip {
namespace {

constexpr size_t kMaxChunk = hg_meter::kMaxWins * 480;   // 320 ms at 24 kHz

struct Slot {
    int16_t pcm[kMaxChunk];
    size_t n;
    int rate;
    uint32_t seq;
};

Slot* g_slots = nullptr;                 // 2 slots in PSRAM
std::atomic<int> g_ready{-1};            // slot index waiting for the task
std::atomic<int> g_busy{-1};             // slot the task is reading
std::atomic<bool> g_active{false};
std::atomic<bool> g_enabled{true};
std::atomic<uint32_t> g_drops{0};
TaskHandle_t g_task = nullptr;
hg_vowel::Analyzer24k* g_an = nullptr;

void Tap(const int16_t* pcm, size_t n, int rate, uint32_t seq) {
    if (!g_active.load() || !g_enabled.load() || g_task == nullptr) return;
    if (rate != 24000 || n == 0) return;   // model and filter are for 24 kHz
    if (n > kMaxChunk) n = kMaxChunk;
    // Alternate slots. The task always takes the newest one, so the slot we
    // write next is never the one it is reading, unless it is two chunks
    // behind; then drop this chunk.
    static int last = 1;
    int slot = last ^ 1;
    if (g_busy.load() == slot) { g_drops.fetch_add(1); return; }
    if (g_ready.load() >= 0) g_drops.fetch_add(1);
    last = slot;
    memcpy(g_slots[slot].pcm, pcm, n * sizeof(int16_t));
    g_slots[slot].n = n;
    g_slots[slot].rate = rate;
    g_slots[slot].seq = seq;
    g_ready.store(slot);
    xTaskNotifyGive(g_task);
}

void Task(void*) {
    auto& m = hg_meter::Get();
    int64_t stat_t0 = esp_timer_get_time();
    int64_t sum_us = 0, max_us = 0;
    uint32_t count = 0;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int slot = g_ready.exchange(-1);
        if (slot < 0) continue;
        g_busy.store(slot);
        const Slot& s = g_slots[slot];
        const size_t win = (size_t)s.rate * hg_meter::kWinMs / 1000;
        int idx = 0;
        for (size_t off = 0; off < s.n && idx < hg_meter::kMaxWins; off += win, ++idx) {
            size_t len = (off + win < s.n) ? win : s.n - off;
            int64_t t0 = esp_timer_get_time();
            g_an->Push(s.pcm + off, len);
            const auto& r = g_an->Analyze();
            int64_t dt = esp_timer_get_time() - t0;
            sum_us += dt; if (dt > max_us) max_us = dt; ++count;
            float c = r.confidence * 255.0f;
            m.vowel[idx].store((uint8_t)r.vowel);
            m.vconf[idx].store((uint8_t)(c > 255.0f ? 255.0f : (c < 0 ? 0 : c)));
        }
        m.vseq.store(s.seq);
        g_busy.store(-1);
        int64_t now = esp_timer_get_time();
        if (now - stat_t0 > 10 * 1000 * 1000 && count > 0) {
            ESP_LOGI(TAG, "vowel analysis: %lu windows, avg %lld us, max %lld us, drops %lu",
                     (unsigned long)count, (long long)(sum_us / count), (long long)max_us,
                     (unsigned long)g_drops.load());
            stat_t0 = now; sum_us = 0; max_us = 0; count = 0;
        }
    }
}

}  // namespace

bool VowelEnabled() { Settings s("hg", false); return s.GetInt("vowel", 1) != 0; }
void SetVowelEnabled(bool on) {
    Settings s("hg", true);
    s.SetInt("vowel", on ? 1 : 0);
    g_enabled.store(on);
}

void SetActive(bool on) {
    if (on == g_active.load()) return;
    if (on && g_an != nullptr) g_an->Reset();   // start each reply clean
    g_active.store(on);
}

void Start() {
    if (g_task != nullptr) return;
    g_enabled.store(VowelEnabled());
    g_slots = static_cast<Slot*>(heap_caps_calloc(2, sizeof(Slot), MALLOC_CAP_SPIRAM));
    void* mem = heap_caps_calloc(1, sizeof(hg_vowel::Analyzer24k), MALLOC_CAP_SPIRAM);
    if (g_slots == nullptr || mem == nullptr) {
        ESP_LOGE(TAG, "no PSRAM for vowel analysis; using loudness only");
        if (g_slots) heap_caps_free(g_slots);
        if (mem) heap_caps_free(mem);
        g_slots = nullptr;
        return;
    }
    g_an = new (mem) hg_vowel::Analyzer24k();
    // v3.6.3: stack in PSRAM (6 KB of internal SRAM back). The task only does
    // math on PSRAM buffers and logs; it never touches flash/NVS.
    if (xTaskCreatePinnedToCoreWithCaps(Task, "hg_lip", 6144, nullptr, 2, &g_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "task start failed; using loudness only");
        g_task = nullptr;
        return;
    }
    hg_meter::Get().tap.store(&Tap);
    ESP_LOGI(TAG, "vowel lip sync ready (%s)", g_enabled.load() ? "on" : "off");
}

}  // namespace hg_lip
