// headphone-girl v3: on-device Settings screen (LVGL, 320x240, touch).
// Open: hold a finger on the screen for 1.5 s, or say "open settings"
// (voice tool self.settings.open). Everything persists in NVS.
#pragma once
#include <functional>

class Display;

namespace hg_settings {

struct Hooks {
    std::function<float()> get_home_pitch;           // degrees, level = 3 (kLevelPitchDeg)
    std::function<void(float)> set_home_pitch;       // move there and save as home
    std::function<void()> go_home;
    std::function<bool()> get_idle_motion;
    std::function<void(bool)> set_idle_motion;
    std::function<bool()> get_follow;
    std::function<void(bool)> set_follow;
    std::function<bool()> get_follow_debug;          // v3.6.2: 1 s debug log line
    std::function<void(bool)> set_follow_debug;
    std::function<void()> enter_wifi_setup;          // reboots into the setup hotspot
};

void Init(Display* display, Hooks hooks);   // after SetupUI
void RequestOpen(const char* page = nullptr);   // any task
void RequestClose();
bool IsOpen();
void NoteActivity();   // v3.1: any raw touch while open resets the 60 s idle close
void RefreshIfOpen();   // v3.6.3: redraw the current page (any task), no-op when closed
void RegisterTools();   // self.settings.open

}  // namespace hg_settings
