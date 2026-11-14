#include "hg_crash.h"

#include <cstdio>
#include <cstring>

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "board.h"
#include "settings.h"

#define TAG "HgCrash"

namespace hg_crash {
namespace {

constexpr uint32_t kMagic = 0x48474331;  // "HGC1"

struct Crumb {
    uint32_t magic;
    uint32_t boot_count;
    uint32_t uptime_s;
    int32_t state;
    int32_t volume;
    int32_t batt;          // -1 unknown
    uint32_t flags;        // 1 charging, 2 discharging, 4 speaking
    uint32_t sram_free;
    uint32_t sram_min;
    uint32_t psram_free;
    char note[32];
};

RTC_NOINIT_ATTR Crumb s_crumb;
Crumb s_prev;
bool s_prev_valid = false;
std::string s_summary;

const char* StateName(int s) {
    // DeviceState enum order in device_state.h
    static const char* names[] = {"unknown", "starting", "wifi-setup", "idle", "connecting",
                                  "listening", "speaking", "notifying", "upgrading",
                                  "activating", "audio-test", "fatal-error"};
    if (s >= 0 && s < (int)(sizeof(names) / sizeof(names[0]))) return names[s];
    return "?";
}

}  // namespace

const char* ReasonName(int r) {
    switch (r) {
        case ESP_RST_POWERON: return "POWER_ON";
        case ESP_RST_EXT: return "EXTERNAL_PIN";
        case ESP_RST_SW: return "SOFTWARE_RESTART";
        case ESP_RST_PANIC: return "PANIC(crash)";
        case ESP_RST_INT_WDT: return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
        case ESP_RST_WDT: return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
        case ESP_RST_BROWNOUT: return "BROWNOUT(low voltage)";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_USB: return "USB(flash/serial)";
        case ESP_RST_JTAG: return "JTAG";
        case ESP_RST_EFUSE: return "EFUSE";
        case ESP_RST_PWR_GLITCH: return "POWER_GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
        default: return "UNKNOWN";
    }
}

bool IsAbnormal(int r) {
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
           r == ESP_RST_WDT || r == ESP_RST_BROWNOUT || r == ESP_RST_PWR_GLITCH ||
           r == ESP_RST_CPU_LOCKUP;
}

void OnBoot() {
    const int reason = (int)esp_reset_reason();
    s_prev_valid = s_crumb.magic == kMagic && reason != ESP_RST_POWERON;
    if (s_prev_valid) {
        s_prev = s_crumb;
        s_prev.note[sizeof(s_prev.note) - 1] = '\0';
    }
    const uint32_t boots = (s_crumb.magic == kMagic ? s_crumb.boot_count : 0) + 1;
    memset(&s_crumb, 0, sizeof(s_crumb));
    s_crumb.magic = kMagic;
    s_crumb.boot_count = boots;
    s_crumb.batt = -1;

    char b[256];
    int n = snprintf(b, sizeof(b), "%s (boot #%u)", ReasonName(reason), (unsigned)boots);
    if (s_prev_valid && n > 0 && n < (int)sizeof(b)) {
        char batt[40];
        if (s_prev.batt >= 0) {
            snprintf(batt, sizeof(batt), "batt %d%%%s", (int)s_prev.batt,
                     (s_prev.flags & 2) ? " on battery" : ((s_prev.flags & 1) ? " charging" : " on USB"));
        } else {
            snprintf(batt, sizeof(batt), "batt ?");
        }
        snprintf(b + n, sizeof(b) - n,
                 "; before it: up %us, %s%s, %s, vol %d, sram %uK (min %uK), psram %uK%s%s",
                 (unsigned)s_prev.uptime_s, StateName(s_prev.state),
                 (s_prev.flags & 4) ? " (audio playing)" : "", batt, (int)s_prev.volume,
                 (unsigned)(s_prev.sram_free / 1024), (unsigned)(s_prev.sram_min / 1024),
                 (unsigned)(s_prev.psram_free / 1024), s_prev.note[0] ? ", note: " : "",
                 s_prev.note);
    }
    s_summary = b;
    if (IsAbnormal(reason)) {
        ESP_LOGE(TAG, "LAST RESET WAS A CRASH: %s", s_summary.c_str());
        Settings s("hg", true);
        s.SetString("last_crash", s_summary);
    } else {
        ESP_LOGW(TAG, "boot reason: %s", s_summary.c_str());
    }
}

void Tick(int device_state, int volume, bool speaking) {
    static uint32_t ticks = 0;
    ++ticks;
    s_crumb.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    s_crumb.state = device_state;
    s_crumb.volume = volume;
    s_crumb.flags = (s_crumb.flags & 3u) | (speaking ? 4u : 0u);
    s_crumb.sram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_crumb.sram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    s_crumb.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (ticks % 10 == 1) {   // battery over I2C: every 10 s is plenty
        int level = -1;
        bool charging = false, discharging = false;
        if (Board::GetInstance().GetBatteryLevel(level, charging, discharging)) {
            s_crumb.batt = level;
            s_crumb.flags = (s_crumb.flags & 4u) | (charging ? 1u : 0u) | (discharging ? 2u : 0u);
        }
    }
}

void Note(const char* text) {
    if (text == nullptr) return;
    strncpy(s_crumb.note, text, sizeof(s_crumb.note) - 1);
    s_crumb.note[sizeof(s_crumb.note) - 1] = '\0';
}

const std::string& BootSummary() { return s_summary; }

std::string LastCrash() {
    Settings s("hg", false);
    return s.GetString("last_crash");
}

}  // namespace hg_crash
