// headphone-girl v3.6.6: why did she reboot? (no serial needed)
//
// At boot: esp_reset_reason() + a breadcrumb that the previous run kept updating
// once a second in RTC memory (survives panic / watchdog / brownout resets, lost
// only on a real power-off). The summary is logged, shown in Settings > About,
// sent to the server inside the WebSocket hello ("hg_boot"), and an abnormal one
// is also kept in NVS (hg/last_crash) so it survives a power cycle.
#pragma once

#include <string>

namespace hg_crash {

// Call once, early in app_main, after nvs_flash_init().
void OnBoot();

// Call once a second from the main loop.
void Tick(int device_state, int volume, bool speaking);

// Short free-text marker kept in the breadcrumb (<= 31 chars).
void Note(const char* text);

// This boot: e.g. "BROWNOUT; before: up 312s state speaking, batt 41% (on battery) ..."
const std::string& BootSummary();
// Last abnormal reset kept in NVS ("" if none).
std::string LastCrash();
// Pure helpers (host-tested).
const char* ReasonName(int reason);
bool IsAbnormal(int reason);

}  // namespace hg_crash
