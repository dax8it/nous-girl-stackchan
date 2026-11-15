// headphone-girl v3: on-device Settings screen. See hg_settings.h.
//
// Threading rules (learned the hard way on this board, see the old menu notes
// in m5stack_core_s3.cc):
//   * LVGL objects are only created/deleted inside the LVGL task, via
//     lv_async_call(). Button callbacks already run there.
//   * Anything slow (SD card, servo moves, Wi-Fi, reboot) runs in a short
//     worker task, then posts a UI refresh back with lv_async_call().
//   * The SD card shares the LCD bus, so SD work never runs in the LVGL task.

#include "hg_settings.h"
#include "cores3_servo.h"   // kLevelPitchDeg
#include "hg_lip.h"
#include "cores3_py32_led.h"

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <nvs_flash.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "application.h"
#include "assets/lang_config.h"
#include "backlight.h"
#include "board.h"
#include "cores3_py32_led.h"
#include "display/display.h"
#include "hg_agent_tools.h"
#include "mcp_server.h"
#include "settings.h"
#include "hg_barge.h"   // v3.6.5
#include "hg_crash.h"   // v3.6.6
#include <ssid_manager.h>   // v3.6.5 saved Wi-Fi list
#include "stackchan_geometry_display.h"

#define TAG "HgSettings"

namespace hg_settings {
namespace {

Display* g_disp = nullptr;
Hooks g_hooks;
std::atomic<bool> g_open{false};
std::atomic<int64_t> g_last_touch_ms{0};
constexpr int64_t kAutoCloseMs = 60 * 1000;   // v3.1: 60 s with no touch at all

lv_obj_t* g_root = nullptr;     // full-screen panel
lv_obj_t* g_title = nullptr;
lv_obj_t* g_back = nullptr;
lv_obj_t* g_body = nullptr;     // scrolling page content
lv_timer_t* g_idle_timer = nullptr;
std::string g_page = "home";
std::string g_pending_page = "home";
int g_light_bright = 80;
bool g_reset_armed = false;

// colours: bright, friendly, readable on the 2" screen
const lv_color_t kBg = lv_color_hex(0x1B1F3B);
const lv_color_t kCard = lv_color_hex(0x2C3263);
const lv_color_t kText = lv_color_hex(0xFFFFFF);
const lv_color_t kMuted = lv_color_hex(0xB8C0FF);
const uint32_t kTileColors[] = {0x3E7BFA, 0xFF7A59, 0x2EC27E, 0xB45CF0, 0xF5A623,
                                0xE84393, 0x00B8D4, 0x8E9AAF, 0x6C5CE7, 0x26A69A,
                                0xEF5350, 0x5C6BC0};

int64_t NowMs() { return esp_timer_get_time() / 1000; }
void Touch() { g_last_touch_ms.store(NowMs()); }
std::atomic<int64_t> g_opened_ms{0};

// ── closures attached to LVGL objects ──
struct Cb { std::function<void(lv_event_t*)> f; };
std::vector<std::unique_ptr<Cb>> g_cbs;   // freed when the page is rebuilt

void Trampoline(lv_event_t* e) {
    Touch();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) ESP_LOGI(TAG, "tap on page %s", g_page.c_str());
    auto* cb = static_cast<Cb*>(lv_event_get_user_data(e));
    if (cb != nullptr && cb->f) cb->f(e);
}
void On(lv_obj_t* obj, lv_event_code_t code, std::function<void(lv_event_t*)> f) {
    g_cbs.push_back(std::make_unique<Cb>(Cb{std::move(f)}));
    lv_obj_add_event_cb(obj, Trampoline, code, g_cbs.back().get());
}

// run slow work off the LVGL task, then optionally refresh the current page
struct Job { std::function<void()> work; bool refresh; };
void Work(std::function<void()> work, bool refresh_after = false) {
    auto* j = new Job{std::move(work), refresh_after};
    if (xTaskCreate([](void* a) {
            auto* job = static_cast<Job*>(a);
            if (job->work) job->work();
            if (job->refresh) RequestOpen(nullptr);
            delete job;
            vTaskDelete(nullptr);
        }, "hg_set_job", 6144, j, 3, nullptr) != pdPASS) {
        delete j;
    }
}

// lv_async_call from a non-LVGL task must hold the LVGL lock
void Post(lv_async_cb_t cb, void* arg) {
    if (g_disp == nullptr) return;
    DisplayLockGuard lock(g_disp);
    lv_async_call(cb, arg);
}

const lv_font_t* Big() { return &lv_font_montserrat_20; }
const lv_font_t* Mid() { return &lv_font_montserrat_16; }

// ── widget helpers (LVGL task only) ──
lv_obj_t* Label(const std::string& text, bool muted = false, lv_obj_t* parent = nullptr) {
    lv_obj_t* l = lv_label_create(parent ? parent : g_body);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_font(l, Mid(), 0);
    lv_obj_set_style_text_color(l, muted ? kMuted : kText, 0);
    lv_label_set_text(l, text.c_str());
    return l;
}

lv_obj_t* Button(lv_obj_t* parent, const std::string& text, uint32_t color,
                 std::function<void()> f, int w = LV_SIZE_CONTENT, int h = 44) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, Mid(), 0);
    lv_obj_set_style_text_color(l, kText, 0);
    lv_label_set_text(l, text.c_str());
    lv_obj_center(l);
    On(b, LV_EVENT_CLICKED, [f](lv_event_t*) { if (f) f(); });
    return b;
}

lv_obj_t* Row() {
    lv_obj_t* r = lv_obj_create(g_body);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_gap(r, 6, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

void SwitchRow(const std::string& text, bool on, std::function<void(bool)> f) {
    lv_obj_t* r = lv_obj_create(g_body);
    lv_obj_set_size(r, lv_pct(100), 48);
    lv_obj_set_style_bg_color(r, kCard, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 12, 0);
    lv_obj_set_style_pad_hor(r, 10, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(r);
    lv_obj_set_style_text_font(l, Mid(), 0);
    lv_obj_set_style_text_color(l, kText, 0);
    lv_label_set_text(l, text.c_str());
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* s = lv_switch_create(r);
    lv_obj_set_size(s, 56, 30);
    lv_obj_align(s, LV_ALIGN_RIGHT_MID, 0, 0);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    On(s, LV_EVENT_VALUE_CHANGED, [f, s](lv_event_t*) { f(lv_obj_has_state(s, LV_STATE_CHECKED)); });
}

void SliderRow(const std::string& text, int lo, int hi, int val, std::function<void(int)> f) {
    Label(text);
    lv_obj_t* s = lv_slider_create(g_body);
    lv_obj_set_width(s, lv_pct(92));
    lv_obj_set_height(s, 16);
    lv_slider_set_range(s, lo, hi);
    lv_slider_set_value(s, val, LV_ANIM_OFF);
    On(s, LV_EVENT_RELEASED, [f, s](lv_event_t*) { f(lv_slider_get_value(s)); });
}

void Go(const char* page);

// ════════════════════════════════ pages ════════════════════════════════
struct Tile { const char* id; const char* text; };
const Tile kTiles[] = {
    {"wifi", LV_SYMBOL_WIFI " Wi-Fi"}, {"wake", LV_SYMBOL_AUDIO " Wake words"},
    {"sound", LV_SYMBOL_VOLUME_MAX " Sound"}, {"screen", LV_SYMBOL_EYE_OPEN " Screen"},
    {"head", LV_SYMBOL_REFRESH " Head"}, {"lights", LV_SYMBOL_CHARGE " Lights"},
    {"dance", LV_SYMBOL_SHUFFLE " Dance"}, {"music", LV_SYMBOL_PLAY " Music"},
    {"server", LV_SYMBOL_UPLOAD " Server"}, {"voice", LV_SYMBOL_BELL " Voice & AI"},
    {"camera", LV_SYMBOL_IMAGE " Camera & SD"}, {"lang", LV_SYMBOL_LIST " Language"},
    {"about", LV_SYMBOL_HOME " About"}, {"power", LV_SYMBOL_POWER " Restart"},
};

void PageHome() {
    lv_obj_t* grid = Row();
    int i = 0;
    for (const auto& t : kTiles) {
        std::string id = t.id;
        Button(grid, t.text, kTileColors[i++ % 12], [id]() { Go(id.c_str()); }, 140, 50);
    }
    Label("Hold the screen 1.5 s to open this again. Say \"open settings\" too.", true);
}

void PageWifi() {
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        char b[96];
        snprintf(b, sizeof(b), "Network: %s", (const char*)ap.ssid);
        Label(b);
        snprintf(b, sizeof(b), "Signal: %d dBm, channel %d", ap.rssi, ap.primary);
        Label(b, true);
    } else {
        Label("Not connected to Wi-Fi");
    }
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    if (nif != nullptr && esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.ip.addr != 0) {
        char b[64];
        snprintf(b, sizeof(b), "IP: " IPSTR, IP2STR(&ip.ip));
        Label(b);
    }
    lv_obj_t* r = Row();
    Button(r, "Reconnect", 0x3E7BFA, []() {
        Work([]() { esp_wifi_disconnect(); vTaskDelay(pdMS_TO_TICKS(300)); esp_wifi_connect(); });
    });
    Button(r, "Wi-Fi setup mode", 0xFF7A59, []() {
        if (g_hooks.enter_wifi_setup) Work([]() { g_hooks.enter_wifi_setup(); });
    });
    // v3.6.5: saved networks (names only; passwords are never shown).
    {
        const auto& list = SsidManager::GetInstance().GetSsidList();
        char b[64];
        snprintf(b, sizeof(b), "Saved networks (%d of 10):", (int)list.size());
        Label(b);
        if (list.empty()) {
            Label("none", true);
        }
        for (size_t i = 0; i < list.size(); ++i) {
            std::string line = std::to_string(i + 1) + ". \"" + list[i].ssid + "\"";
            Label(line, true);
        }
    }
    Label("Setup mode makes her hotspot (Xiaozhi-XXXX) so you can add a network. "
          "Only 2.4 GHz works: on iPhone turn on Maximize Compatibility. "
          "In setup mode she keeps looking and joins any saved network that appears.", true);
}

void PageLang() {
    Label("Language: English (US)");
    Label("Only English is built in. Adding more languages means a different firmware "
          "build (extra voice prompts and fonts), so it is not switchable here.", true);
}

std::string CurrentOtaUrl() {
    Settings s("wifi", false);
    std::string u = s.GetString("ota_url");
    return u.empty() ? std::string(CONFIG_OTA_URL) : u;
}

void SaveOtaAndRestart(const std::string& url) {
    {
        Settings s("wifi", true);
        s.SetString("ota_url", url);
    }
    ESP_LOGW(TAG, "OTA URL set to %s, restarting", url.c_str());
    Work([]() { vTaskDelay(pdMS_TO_TICKS(600)); esp_restart(); });
}

void PageServer() {
    std::string cur = CurrentOtaUrl();
    Label("Server (OTA) address now:");
    Label(cur, true);
    lv_obj_t* ta = lv_textarea_create(g_body);
    lv_obj_set_width(ta, lv_pct(100));
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_text_font(ta, Mid(), 0);
    lv_textarea_set_text(ta, cur.c_str());
    lv_obj_t* r = Row();
    Button(r, "Home server", 0x2EC27E, [ta]() {
        lv_textarea_set_text(ta, "https://your-server.example.invalid/xiaozhi/ota/?k=");
    });
    Button(r, "xiaozhi.me", 0x8E9AAF, [ta]() {
        lv_textarea_set_text(ta, "https://api.tenclass.net/xiaozhi/ota/");
    });
    Button(r, "Save + restart", 0xFF7A59, [ta]() {
        std::string u = lv_textarea_get_text(ta);
        while (!u.empty() && (u.back() == ' ' || u.back() == '\n')) u.pop_back();
        // v3.3: refuse an unfinished Home server address (empty key after k=)
        bool key_missing = u.size() >= 3 && u.compare(u.size() - 3, 3, "?k=") == 0;
        if ((u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0) && !key_missing) {
            SaveOtaAndRestart(u);
        } else {
            ESP_LOGW(TAG, "server URL not saved (bad or missing key): %s", u.c_str());
        }
    });
    Label("Replace the example address with your server's OTA URL and key if needed. "
          "You can also use Wi-Fi > setup mode, "
          "then paste the whole URL from your phone.", true);
    lv_obj_t* kb = lv_keyboard_create(g_root);
    lv_obj_set_size(kb, lv_pct(100), 110);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    // v3.3: open the keyboard on tap too (FOCUSED alone can miss without an
    // input group), and keep the box visible above the keyboard.
    auto show_kb = [kb, ta](lv_event_t*) {
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb);
        lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
    };
    On(ta, LV_EVENT_FOCUSED, show_kb);
    On(ta, LV_EVENT_CLICKED, show_kb);
    On(kb, LV_EVENT_READY, [kb](lv_event_t*) { lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); });
    On(kb, LV_EVENT_CANCEL, [kb](lv_event_t*) { lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); });
    // the keyboard lives on g_root; delete it with the page
    On(g_body, LV_EVENT_DELETE, [kb](lv_event_t*) { lv_obj_delete_async(kb); });
}

void PageVoice() {
    // v3.6.1: the old kokoro / elevenlabs / openai selector did nothing. Gone.
    Label("Voice is set on the server.", false);
    Label("Her voice, brain (Hermes) and hearing all run on the Mac server and are "
          "chosen there (.config.yaml), not on the robot.", true);
}

void PageSound() {
    auto* codec = Board::GetInstance().GetAudioCodec();
    int vol = codec ? codec->output_volume() : 70;
    char b[96];
    snprintf(b, sizeof(b), "Volume %d, speaker %s, %d Hz", vol,
             codec && codec->output_enabled() ? "on" : "idle",
             codec ? codec->output_sample_rate() : 0);
    Label(b, true);
    // v3.3: never let the slider reach 0 by accident (0 = silent forever)
    SliderRow("Volume", 10, 100, vol < 10 ? 10 : vol, [](int v) {
        if (auto* c = Board::GetInstance().GetAudioCodec()) c->SetOutputVolume(v);
    });
    lv_obj_t* r = Row();
    // v3.3: speaker self-test, no network involved. If you hear the two beeps
    // and the pop, the speaker works and any silence is in the network audio.
    Button(r, LV_SYMBOL_AUDIO " Test speaker", 0x2EC27E, []() {
        Work([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() != kDeviceStateIdle) {
                ESP_LOGW(TAG, "test speaker: not idle (state %d), playing anyway", (int)app.GetDeviceState());
            }
            app.GetAudioService().PlayTestTone();
            vTaskDelay(pdMS_TO_TICKS(900));
            app.PlaySound(Lang::Sounds::OGG_POPUP);
        });
    });
    Button(r, "Volume 80", 0x3E7BFA, []() {
        if (auto* c = Board::GetInstance().GetAudioCodec()) c->SetOutputVolume(80);
        Go("sound");
    });
    Label("Test speaker plays two beeps, then a pop, straight from her memory. "
          "Beeps OK but no voice = network audio problem, not the speaker.", true);
}

void PageScreen() {
    auto* bl = Board::GetInstance().GetBacklight();
    int b = bl ? bl->brightness() : 70;
    SliderRow("Brightness", 10, 100, b, [](int v) {
        if (auto* l = Board::GetInstance().GetBacklight()) l->SetBrightness((uint8_t)v, true);
    });
    SwitchRow("Lip sync (mouth follows voice)", StackChanGeometryDisplay::LipSyncEnabled(),
              [](bool on) { StackChanGeometryDisplay::SetLipSyncEnabled(on); });
    // v3.5
    SwitchRow("Vowel mouth shapes (a i u e o)", hg_lip::VowelEnabled(),
              [](bool on) { hg_lip::SetVowelEnabled(on); });
    SwitchRow("Talking lights", CoreS3Py32Led::TalkLightsEnabled(), [](bool on) {
        if (auto* l = static_cast<CoreS3Py32Led*>(Board::GetInstance().GetLed())) {
            l->SetTalkLightsEnabled(on);
        }
    });
}

void PageHead() {
    float hp = g_hooks.get_home_pitch ? g_hooks.get_home_pitch() : kLevelPitchDeg;
    char b[80];
    snprintf(b, sizeof(b), "Rest tilt: %.0f deg (3 = level, facing forward)", hp);
    Label(b);
    lv_obj_t* r = Row();
    Button(r, LV_SYMBOL_DOWN " Lower", 0x3E7BFA, [hp]() {
        Work([hp]() { if (g_hooks.set_home_pitch) g_hooks.set_home_pitch(hp - 1.0f); }, true);
    });
    Button(r, LV_SYMBOL_UP " Raise", 0x3E7BFA, [hp]() {
        Work([hp]() { if (g_hooks.set_home_pitch) g_hooks.set_home_pitch(hp + 1.0f); }, true);
    });
    lv_obj_t* r2 = Row();
    Button(r2, "Go home", 0x2EC27E, []() { Work([]() { if (g_hooks.go_home) g_hooks.go_home(); }); });
    Button(r2, "Reset to level", 0xFF7A59, []() {
        Work([]() { if (g_hooks.set_home_pitch) g_hooks.set_home_pitch(kLevelPitchDeg); }, true);
    });
    if (g_hooks.get_idle_motion)
        SwitchRow("Look around when idle", g_hooks.get_idle_motion(),
                  [](bool on) { if (g_hooks.set_idle_motion) g_hooks.set_idle_motion(on); });
    if (g_hooks.get_follow)
        SwitchRow("Follow faces (camera)", g_hooks.get_follow(),
                  [](bool on) { if (g_hooks.set_follow) g_hooks.set_follow(on); });
    if (g_hooks.get_follow_debug)
        SwitchRow("Follow faces debug (log once a second)", g_hooks.get_follow_debug(),
                  [](bool on) { if (g_hooks.set_follow_debug) g_hooks.set_follow_debug(on); });
    // v3.5: swipe forward = next song or dance, back = stop. Flip if it feels backwards.
    {
        Settings sw("hg", false);
        SwitchRow("Swap head swipe direction", sw.GetInt("swipeswap", 0) != 0,
                  [](bool on) { Settings w("hg", true); w.SetInt("swipeswap", on ? 1 : 0); });
    }
}

void Light(const char* action, const char* color, const char* mood, const char* cycle, bool fast) {
    auto* led = static_cast<CoreS3Py32Led*>(Board::GetInstance().GetLed());
    if (led != nullptr) led->ApplyLight(action, color, mood, cycle, fast, g_light_bright);
}

void PageLights() {
    lv_obj_t* r = Row();
    Button(r, "On", 0xF5A623, []() { Light("on", "", "", "", false); });
    Button(r, "Off", 0x8E9AAF, []() { Light("off", "", "", "", false); });
    Button(r, "Auto", 0x2EC27E, []() { Light("normal", "", "", "", false); });
    Label("Colors", true);
    lv_obj_t* c = Row();
    struct C { const char* n; uint32_t hex; };
    for (C k : {C{"red", 0xE53935}, C{"orange", 0xFB8C00}, C{"yellow", 0xFDD835},
                C{"green", 0x43A047}, C{"blue", 0x1E88E5}, C{"purple", 0x8E24AA},
                C{"pink", 0xEC407A}, C{"white", 0x9E9E9E}}) {
        std::string n = k.n;
        Button(c, n, k.hex, [n]() { Light("color", n.c_str(), "", "", false); }, 66, 40);
    }
    Label("Moods", true);
    lv_obj_t* m = Row();
    for (const char* md : {"happy", "calm", "party", "spooky", "sleepy", "focus"}) {
        std::string n = md;
        Button(m, n, 0x6C5CE7, [n]() { Light("mood", "", n.c_str(), "", false); }, 92, 40);
    }
    lv_obj_t* cy = Row();
    Button(cy, "Rainbow slow", 0x00B8D4, []() { Light("cycle", "", "", "rainbow", false); });
    Button(cy, "Rainbow fast", 0xE84393, []() { Light("cycle", "", "", "rainbow", true); });
    SliderRow("Light brightness (next change)", 5, 100, g_light_bright,
              [](int v) { g_light_bright = v; });
}

void PageWake() {
    Settings s("hg", false);
    bool all = s.GetInt("ww_on", 1) != 0;
    SwitchRow("Wake words on", all, [](bool on) { Settings w("hg", true); w.SetInt("ww_on", on); });
    SwitchRow("\"Hey Nous-Girl\"", s.GetInt("ww_nous", 1) != 0,
              [](bool on) { Settings w("hg", true); w.SetInt("ww_nous", on); });
    SwitchRow("\"Hey Jarvis\"", s.GetInt("ww_jarvis", 1) != 0,
              [](bool on) { Settings w("hg", true); w.SetInt("ww_jarvis", on); });
    SwitchRow("\"Hey YuB\" (try it)", s.GetInt("ww_yub", 0) != 0,
              [](bool on) { Settings w("hg", true); w.SetInt("ww_yub", on); });
    // v3.6.5: barge-in with "Hey Nous-Girl" (off by default, applies right away).
    if (hg_barge::kBuildAllowsBarge) {
        SwitchRow("Interrupt with wake word", hg_barge::Enabled(), [](bool on) {
            Settings w("hg", true);
            w.SetInt("ww_barge", on ? 1 : 0);
            hg_barge::SetEnabled(on);
            ESP_LOGI(TAG, "settings: interrupt with wake word -> %s", on ? "on" : "off");
        });
        Label("On: say \"Hey Nous-Girl\" while she talks to stop her and listen. "
              "No echo cancel: her own voice can cause a false stop or a missed one.", true);
    } else {
        // v3.6.6: hidden; it did not work and is a crash risk while she talks.
        Label("To stop her while she talks: tap the screen.", true);
    }
    int sens = s.GetInt("ww_sens", 1);
    Label("Sensitivity (high = hears you easier, wakes by mistake more)", true);
    lv_obj_t* r = Row();
    const char* names[] = {"Low", "Normal", "High"};
    for (int i = 0; i < 3; ++i) {
        Button(r, names[i], i == sens ? 0x2EC27E : 0x3E7BFA, [i]() {
            Settings w("hg", true);
            w.SetInt("ww_sens", i);
            Go("wake");
        }, 90, 40);
    }
    lv_obj_t* r2 = Row();
    Button(r2, "Restart to apply", 0xFF7A59, []() {
        Work([]() { vTaskDelay(pdMS_TO_TICKS(400)); esp_restart(); });
    });
}

void PageDance() {
    Label("Dances (head + lights + face)", true);
    lv_obj_t* r = Row();
    for (const auto& n : hg_agent::PresetNames()) {
        Button(r, n, 0xB45CF0, [n]() {
            Work([n]() { std::string why; hg_agent::PlayPreset(n, 2, &why); });
        }, 92, 40);
    }
    lv_obj_t* r2 = Row();
    if (hg_agent::IsRecording()) {
        Button(r2, LV_SYMBOL_STOP " Stop + save", 0xEF5350, []() {
            Work([]() { std::string p, why; hg_agent::StopRecording(&p, &why); }, true);
        });
    } else {
        Button(r2, "Learn a dance", 0xFF7A59, []() {
            Work([]() { std::string n, why; hg_agent::StartRecording(&n, &why); }, true);
        });
    }
    Button(r2, "Stop", 0x8E9AAF, []() { hg_agent::StopDance(); });
    Label("Learn: move her head with the remote joystick, then Stop + save (up to 60 s, "
          "saved on the SD card).", true);
    Label("My dances (SD card): loading...", true);
    Work([]() {
        auto list = new std::vector<std::string>(hg_agent::ListDances());
        Post([](void* a) {
            auto* l = static_cast<std::vector<std::string>*>(a);
            if (g_open.load() && g_page == "dance" && g_body != nullptr) {
                lv_obj_t* last = lv_obj_get_child(g_body, -1);
                if (last) lv_label_set_text(last, l->empty() ? "My dances: none yet" : "My dances:");
                lv_obj_t* row = Row();
                for (const auto& n : *l) {
                    std::string nn = n;
                    Button(row, nn, 0x26A69A, [nn]() {
                        Work([nn]() { std::string why; hg_agent::PlaySavedDance(nn, &why); });
                    }, LV_SIZE_CONTENT, 40);
                }
            }
            delete l;
        }, list);
    });
}

void PageMusic() {
    SwitchRow("Dance to music", hg_agent::GrooveEnabled(),
              [](bool on) { hg_agent::SetGrooveEnabled(on); });
    // v3.6.7-dj
    SwitchRow("DJ mode (DJ screen + groove while a Mac song plays)", hg_agent::DjEnabled(),
              [](bool on) { hg_agent::SetDjEnabled(on); });
    lv_obj_t* r = Row();
    Button(r, LV_SYMBOL_SHUFFLE " Random", 0x2EC27E, []() {
        Work([]() { std::string why; hg_agent::PlaySdMusic("", &why); });
    });
    Button(r, LV_SYMBOL_STOP " Stop", 0xEF5350, []() { hg_agent::StopSdMusic(); });
    if (hg_agent::SdMusicPlaying()) Label("Playing: " + hg_agent::SdMusicNow());
    Label("Songs on the SD card (/music): loading...", true);
    Work([]() {
        auto list = new std::vector<std::string>(hg_agent::ListMusic(24));
        Post([](void* a) {
            auto* l = static_cast<std::vector<std::string>*>(a);
            if (g_open.load() && g_page == "music" && g_body != nullptr) {
                lv_obj_t* last = lv_obj_get_child(g_body, -1);
                if (last) lv_label_set_text(last, l->empty()
                    ? "No MP3s in /music on the SD card. Songs on the Mac: say \"play music\"."
                    : "Songs on the SD card:");
                for (const auto& n : *l) {
                    std::string nn = n;
                    Button(g_body, LV_SYMBOL_PLAY " " + nn, 0x3E7BFA, [nn]() {
                        Work([nn]() { std::string why; hg_agent::PlaySdMusic(nn, &why); });
                    }, lv_pct(100), 40);
                }
            }
            delete l;
        }, list);
    });
}

void PageCamera() {
    SwitchRow("Camera", hg_agent::CameraEnabled(), [](bool on) { hg_agent::SetCameraEnabled(on); });
    Label("SD card: checking...", true);
    Work([]() {
        auto* t = new std::string(hg_agent::SdStatusText());
        Post([](void* a) {
            auto* s = static_cast<std::string*>(a);
            if (g_open.load() && g_page == "camera" && g_body != nullptr) {
                lv_obj_t* last = lv_obj_get_child(g_body, -1);
                if (last) lv_label_set_text(last, ("SD card: " + *s).c_str());
            }
            delete s;
        }, t);
    });
}

void PageAbout() {
    const esp_app_desc_t* d = esp_app_get_description();
    char b[160];
    snprintf(b, sizeof(b), "Mr RoBeast - headphone-girl v3.6.7-dj7\nFirmware %s (%s)", d->version, d->date);
    Label(b);
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(b, sizeof(b), "MAC %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Label(b, true);
    snprintf(b, sizeof(b), "Free RAM %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    Label(b, true);
    Label("Server: " + CurrentOtaUrl(), true);
    // v3.6.6: why she last restarted (no serial cable needed).
    Label("Started because: " + hg_crash::BootSummary(), true);
    std::string last = hg_crash::LastCrash();
    Label("Last crash: " + (last.empty() ? std::string("none recorded") : last), true);
}

void PagePower() {
    lv_obj_t* r = Row();
    Button(r, LV_SYMBOL_REFRESH " Restart", 0x3E7BFA, []() {
        Work([]() { vTaskDelay(pdMS_TO_TICKS(300)); esp_restart(); });
    });
    Button(r, g_reset_armed ? "Tap again: ERASE ALL" : "Factory reset", 0xEF5350, []() {
        if (!g_reset_armed) { g_reset_armed = true; Go("power"); return; }
        Work([]() {
            ESP_LOGW(TAG, "factory reset: erasing NVS");
            nvs_flash_erase();
            esp_restart();
        });
    });
    Label("Factory reset forgets Wi-Fi, server address and every setting here. "
          "Your SD card and songs are not touched.", true);
}

struct PageDef { const char* id; const char* title; void (*build)(); };
const PageDef kPages[] = {
    {"home", "Settings", PageHome}, {"wifi", "Wi-Fi", PageWifi}, {"lang", "Language", PageLang},
    {"server", "Server", PageServer}, {"voice", "Voice & AI", PageVoice},
    {"sound", "Sound", PageSound}, {"screen", "Screen", PageScreen}, {"head", "Head", PageHead},
    {"lights", "Lights", PageLights}, {"wake", "Wake words", PageWake},
    {"dance", "Dance", PageDance}, {"music", "Music", PageMusic},
    {"camera", "Camera & SD", PageCamera}, {"about", "About", PageAbout},
    {"power", "Restart", PagePower},
};

void CloseAsync(void*);

void BuildRoot() {
    if (g_root != nullptr) return;
    lv_obj_t* top = lv_layer_top();
    g_root = lv_obj_create(top);
    lv_obj_set_size(g_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_root, kBg, 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_root, 0, 0);
    lv_obj_set_style_radius(g_root, 0, 0);
    lv_obj_set_style_pad_all(g_root, 0, 0);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* bar = lv_obj_create(g_root);
    lv_obj_set_size(bar, lv_pct(100), 40);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, kCard, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_back = lv_button_create(bar);
    lv_obj_set_size(g_back, 56, 32);
    lv_obj_align(g_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_back, lv_color_hex(0x3E7BFA), 0);
    lv_obj_t* bl = lv_label_create(g_back);
    lv_obj_set_style_text_font(bl, Big(), 0);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);
    lv_obj_add_event_cb(g_back, [](lv_event_t*) { Touch(); Go("home"); }, LV_EVENT_CLICKED, nullptr);

    g_title = lv_label_create(bar);
    lv_obj_set_style_text_font(g_title, Big(), 0);
    lv_obj_set_style_text_color(g_title, kText, 0);
    lv_obj_align(g_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* x = lv_button_create(bar);
    lv_obj_set_size(x, 56, 32);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(0xEF5350), 0);
    lv_obj_t* xl = lv_label_create(x);
    lv_obj_set_style_text_font(xl, Big(), 0);
    lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_center(xl);
    lv_obj_add_event_cb(x, [](lv_event_t*) { RequestClose(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);

    // v3.1: one 500 ms LVGL timer does two jobs while Settings is open:
    //   * keeps the panel visible and on top, so a cloud/server alert, emotion
    //     change or chat message drawn later can never cover or "close" it;
    //   * closes after kAutoCloseMs with no finger on the glass. Any raw touch
    //     (taps, scrolls, slider drags) counts, via NoteActivity() from the
    //     board's touch poll, not just button clicks.
    g_idle_timer = lv_timer_create([](lv_timer_t*) {
        if (!g_open.load() || g_root == nullptr) return;
        if (lv_obj_has_flag(g_root, LV_OBJ_FLAG_HIDDEN)) {
            ESP_LOGW(TAG, "settings panel was hidden by someone else; showing it again");
            lv_obj_remove_flag(g_root, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_t* top = lv_layer_top();
        uint32_t n = lv_obj_get_child_count(top);
        if (n > 0 && lv_obj_get_child(top, n - 1) != g_root) lv_obj_move_foreground(g_root);
        if (NowMs() - g_last_touch_ms.load() > kAutoCloseMs) {
            ESP_LOGI(TAG, "no touch for %d s, closing settings", (int)(kAutoCloseMs / 1000));
            CloseAsync(nullptr);
        }
    }, 500, nullptr);
}

void ShowPage(const std::string& id) {
    const PageDef* pd = &kPages[0];
    for (const auto& p : kPages) if (id == p.id) pd = &p;
    if (pd->id != std::string("power")) g_reset_armed = false;
    if (g_body != nullptr) {
        lv_obj_delete(g_body);        // also fires LV_EVENT_DELETE handlers
        g_body = nullptr;
    }
    g_cbs.clear();
    g_page = pd->id;
    g_body = lv_obj_create(g_root);
    lv_obj_set_size(g_body, lv_pct(100), 200);
    lv_obj_align(g_body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(g_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_body, 0, 0);
    lv_obj_set_style_pad_all(g_body, 8, 0);
    lv_obj_set_style_pad_gap(g_body, 8, 0);
    lv_obj_set_flex_flow(g_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_body, LV_DIR_VER);
    lv_label_set_text(g_title, pd->title);
    if (std::string(pd->id) == "home") lv_obj_add_flag(g_back, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(g_back, LV_OBJ_FLAG_HIDDEN);
    pd->build();
}

// deferred so a button can switch pages without deleting itself mid-callback
void Go(const char* page) {
    g_pending_page = page;
    lv_async_call([](void*) { if (g_open.load()) ShowPage(g_pending_page); }, nullptr);
}

void OpenAsync(void*) {
    BuildRoot();
    ShowPage(g_pending_page);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_root);
    g_open.store(true);
    g_opened_ms.store(NowMs());
    Touch();
    ESP_LOGI(TAG, "settings open: page %s", g_page.c_str());
}

void CloseAsync(void*) {
    if (g_root == nullptr) return;
    if (g_body != nullptr) { lv_obj_delete(g_body); g_body = nullptr; }
    g_cbs.clear();
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);
    g_open.store(false);
    g_page = "home";
    ESP_LOGI(TAG, "settings closed");
}

}  // namespace

void Init(Display* display, Hooks hooks) {
    g_disp = display;
    g_hooks = std::move(hooks);
}

void RequestOpen(const char* page) {
    if (g_disp == nullptr) return;
    if (page != nullptr) g_pending_page = page;
    else if (!g_open.load()) g_pending_page = "home";
    else g_pending_page = g_page;          // refresh current page
    DisplayLockGuard lock(g_disp);
    lv_async_call(OpenAsync, nullptr);
}

void RequestClose() {
    if (g_disp == nullptr) return;
    DisplayLockGuard lock(g_disp);
    lv_async_call(CloseAsync, nullptr);
}

bool IsOpen() { return g_open.load(); }

void RefreshIfOpen() {
    if (!g_open.load()) return;
    RequestOpen(nullptr);   // open already: re-shows the current page with fresh switch states
}

void NoteActivity() { if (g_open.load()) Touch(); }

void RegisterTools() {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool("self.settings.open",
        "Open (or close) the settings screen on her display. page: home | wifi | wake | sound | "
        "screen | head | lights | dance | music | server | voice | camera | lang | about | power "
        "| close.",
        PropertyList({Property("page", kPropertyTypeString, std::string("home"))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string page = p["page"].value<std::string>();
            if (page == "close") { RequestClose(); return std::string("{\"ok\":true}"); }
            bool known = false;
            for (const auto& d : kPages) if (page == d.id) known = true;
            RequestOpen(known ? page.c_str() : "home");
            return std::string("{\"ok\":true}");
        });
}

}  // namespace hg_settings
