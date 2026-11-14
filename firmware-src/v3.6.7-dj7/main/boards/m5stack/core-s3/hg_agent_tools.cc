// headphone-girl build: device-side agent tools
//
// CoreS3 microSD shares SPI3 with the ILI9342 LCD: SCK=36, MOSI=37, and
// GPIO35 is BOTH the LCD D/C line and the SD MISO line. SD access is safe
// only while the LCD is idle, so every SD operation:
//   1. takes the display (LVGL) lock - LVGL 9 finishes its flush before
//      releasing it, so no LCD transfer is in flight,
//   2. switches GPIO35 to input (SD drives it as MISO),
//   3. mounts FAT at /sd, does the work, unmounts,
//   4. switches GPIO35 back to output for the LCD and unlocks.
// The face animation pauses for the ~100-400 ms this takes.
//
// Every path is relative to /sd/agent; "..", absolute paths and odd
// characters are rejected. Writes are size-capped and extension-limited.

#include "hg_agent_tools.h"

#include <dirent.h>
#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>
#include <cJSON.h>

#include <atomic>
#include <functional>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "camera.h"
#include "cores3_servo.h"
#include "display/display.h"
#include "mcp_server.h"
#include "application.h"
#include "board.h"
#include "settings.h"
#include "cores3_py32_led.h"
#include "audio/hg_audio_meter.h"
#include "esp_mp3_dec.h"
#include "esp_ae_rate_cvt.h"
#include "hg_dj_logic.h"   // v3.6.7-dj
#include <algorithm>
#include <cmath>

#define TAG "HgAgent"

namespace hg_agent {
namespace {

constexpr gpio_num_t kSdCs = GPIO_NUM_4;
constexpr gpio_num_t kSharedDcMiso = GPIO_NUM_35;
constexpr const char* kMount = "/sd";
constexpr const char* kRoot = "/sd/agent";
constexpr size_t kMaxText = 8 * 1024;
constexpr size_t kMaxProfile = 4 * 1024;
constexpr size_t kMaxFace = 16 * 1024;
constexpr size_t kMaxDanceRows = 1200;   // 2 min at 100 ms

Display* g_display = nullptr;
Camera* g_camera = nullptr;
void UpdateParty();   // v3.5: music or dance running -> party face
CoreS3Servo* g_servo = nullptr;
std::mutex g_sd_mutex;

// ── SD session (lock display, flip GPIO35, mount) ───────────────────────────
class SdSession {
public:
    SdSession() : lock_(g_sd_mutex) {
        if (g_display == nullptr) { err_ = "no display"; return; }
        display_locked_ = DisplayLock();
        if (!display_locked_) { err_ = "display busy"; return; }
        gpio_set_direction(kSharedDcMiso, GPIO_MODE_INPUT);

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SPI3_HOST;
        host.max_freq_khz = 20000;
        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.gpio_cs = kSdCs;
        slot.host_id = SPI3_HOST;
        esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
        mcfg.format_if_mount_failed = false;   // never wipe a card
        mcfg.max_files = 3;
        mcfg.allocation_unit_size = 16 * 1024;
        esp_err_t e = esp_vfs_fat_sdspi_mount(kMount, &host, &slot, &mcfg, &card_);
        if (e != ESP_OK) {
            err_ = std::string("sd mount failed: ") + esp_err_to_name(e);
            card_ = nullptr;
            return;
        }
        mkdir(kRoot, 0775);
        ok_ = true;
    }
    ~SdSession() {
        if (card_ != nullptr) esp_vfs_fat_sdcard_unmount(kMount, card_);
        if (display_locked_) {
            gpio_set_direction(kSharedDcMiso, GPIO_MODE_OUTPUT);
            gpio_set_level(kSdCs, 1);
            DisplayUnlock();
        }
    }
    bool ok() const { return ok_; }
    const std::string& error() const { return err_; }
    uint64_t CardBytes() const {
        return card_ ? (uint64_t)card_->csd.capacity * card_->csd.sector_size : 0;
    }

private:
    // DisplayLockGuard is RAII-only; keep one alive for the session.
    bool DisplayLock() { guard_ = new DisplayLockGuard(g_display); return true; }
    void DisplayUnlock() { delete guard_; guard_ = nullptr; }
    std::lock_guard<std::mutex> lock_;
    DisplayLockGuard* guard_ = nullptr;
    bool display_locked_ = false;
    sdmmc_card_t* card_ = nullptr;
    bool ok_ = false;
    std::string err_;
};

// ── path rules ─────────────────────────────────────────────────────────────
bool SafeRel(const std::string& rel, std::string* full, std::string* why) {
    if (rel.size() > 96) { *why = "path too long"; return false; }
    if (!rel.empty() && rel[0] == '/') { *why = "use a path relative to /sd/agent"; return false; }
    if (rel.find("..") != std::string::npos) { *why = "'..' not allowed"; return false; }
    int depth = 0;
    for (char c : rel) {
        bool okc = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/';
        if (!okc) { *why = "only letters, digits, _ - . / allowed"; return false; }
        if (c == '/') depth++;
    }
    if (depth > 3) { *why = "too deep"; return false; }
    *full = rel.empty() ? std::string(kRoot) : std::string(kRoot) + "/" + rel;
    return true;
}
bool SafeId(const std::string& id) {
    if (id.empty() || id.size() > 40) return false;
    for (char c : id) {
        bool okc = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!okc) return false;
    }
    return true;
}
bool EndsWith(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
bool TextExtOk(const std::string& p) {
    return EndsWith(p, ".txt") || EndsWith(p, ".json") || EndsWith(p, ".md") ||
           EndsWith(p, ".csv");
}
void MkdirsFor(const std::string& full) {
    for (size_t i = strlen(kRoot) + 1; i < full.size(); i++) {
        if (full[i] == '/') mkdir(full.substr(0, i).c_str(), 0775);
    }
}
std::string Err(const std::string& m) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", false);
    cJSON_AddStringToObject(o, "error", m.c_str());
    char* s = cJSON_PrintUnformatted(o);
    std::string r = s ? s : "{\"ok\":false}";
    cJSON_free(s); cJSON_Delete(o);
    return r;
}
std::string OkJson(cJSON* o) {
    cJSON_AddBoolToObject(o, "ok", true);
    char* s = cJSON_PrintUnformatted(o);
    std::string r = s ? s : "{\"ok\":true}";
    cJSON_free(s); cJSON_Delete(o);
    return r;
}
bool WriteFile(const std::string& full, const std::string& data, bool append) {
    MkdirsFor(full);
    FILE* f = fopen(full.c_str(), append ? "ab" : "wb");
    if (!f) return false;
    size_t n = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return n == data.size();
}
bool ReadFile(const std::string& full, size_t cap, std::string* out, std::string* why) {
    struct stat st;
    if (stat(full.c_str(), &st) != 0) { *why = "not found"; return false; }
    if ((size_t)st.st_size > cap) { *why = "file too large to read"; return false; }
    FILE* f = fopen(full.c_str(), "rb");
    if (!f) { *why = "open failed"; return false; }
    out->resize(st.st_size);
    size_t n = fread(out->data(), 1, st.st_size, f);
    fclose(f);
    out->resize(n);
    return true;
}
cJSON* ListDir(const std::string& full) {
    cJSON* arr = cJSON_CreateArray();
    DIR* d = opendir(full.c_str());
    if (!d) return arr;
    struct dirent* e;
    int count = 0;
    while ((e = readdir(d)) != nullptr && count < 100) {
        if (e->d_name[0] == '.') continue;
        std::string p = full + "/" + e->d_name;
        struct stat st;
        cJSON* it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "name", e->d_name);
        if (stat(p.c_str(), &st) == 0) {
            cJSON_AddBoolToObject(it, "dir", S_ISDIR(st.st_mode));
            cJSON_AddNumberToObject(it, "bytes", (double)st.st_size);
        }
        cJSON_AddItemToArray(arr, it);
        count++;
    }
    closedir(d);
    return arr;
}

// ── dances ─────────────────────────────────────────────────────────────────
struct DanceRow { uint32_t t_ms; int16_t yaw10; int16_t pitch10; };
std::atomic<bool> g_recording{false};
std::atomic<bool> g_playing{false};
std::vector<DanceRow> g_rec;
std::mutex g_rec_mutex;
std::string g_rec_name;
int g_rec_sample_ms = 100;
int g_rec_max_s = 60;

void RecordTask(void*) {
    int64_t t0 = esp_timer_get_time() / 1000;
    while (g_recording.load()) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now - t0 > (int64_t)g_rec_max_s * 1000) break;
        if (g_servo != nullptr && g_servo->IsReady()) {
            DanceRow r{(uint32_t)(now - t0), (int16_t)(g_servo->GetYawAngle() * 10),
                       (int16_t)(g_servo->GetPitchAngle() * 10)};
            std::lock_guard<std::mutex> l(g_rec_mutex);
            if (g_rec.size() < kMaxDanceRows) g_rec.push_back(r);
        }
        vTaskDelay(pdMS_TO_TICKS(g_rec_sample_ms));
    }
    g_recording.store(false);
    vTaskDelete(nullptr);
}

std::string DanceToCsv(const std::vector<DanceRow>& rows, int sample_ms) {
    std::string out = "# hg-dance v1 sample_ms=" + std::to_string(sample_ms) + "\n";
    out += "t_ms,yaw,pitch\n";
    char line[48];
    for (const auto& r : rows) {
        snprintf(line, sizeof(line), "%lu,%.1f,%.1f\n", (unsigned long)r.t_ms,
                 r.yaw10 / 10.0f, r.pitch10 / 10.0f);
        out += line;
    }
    return out;
}

bool CsvToDance(const std::string& csv, std::vector<DanceRow>* rows) {
    size_t pos = 0;
    while (pos < csv.size() && rows->size() < kMaxDanceRows) {
        size_t nl = csv.find('\n', pos);
        std::string line = csv.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? csv.size() : nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == 't') continue;
        unsigned long t = 0; float y = 0, p = 0;
        if (sscanf(line.c_str(), "%lu,%f,%f", &t, &y, &p) == 3) {
            rows->push_back(DanceRow{(uint32_t)t, (int16_t)(y * 10), (int16_t)(p * 10)});
        }
    }
    return !rows->empty();
}

struct PlayArgs { std::vector<DanceRow> rows; float speed; int loops; };
void PlayTask(void* arg) {
    auto* a = static_cast<PlayArgs*>(arg);
    UpdateParty();   // v3.5: party face while a saved dance plays
    for (int loop = 0; loop < a->loops && g_playing.load(); loop++) {
        int64_t t0 = esp_timer_get_time() / 1000;
        for (const auto& r : a->rows) {
            if (!g_playing.load()) break;
            int64_t due = t0 + (int64_t)(r.t_ms / a->speed);
            int64_t now = esp_timer_get_time() / 1000;
            if (due > now) vTaskDelay(pdMS_TO_TICKS(due - now));
            if (g_servo != nullptr && g_servo->IsReady()) {
                g_servo->MoveTo(r.yaw10 / 10.0f, r.pitch10 / 10.0f, 900);
            }
        }
    }
    if (g_servo != nullptr && g_servo->IsReady()) g_servo->Center(400);
    g_playing.store(false);
    UpdateParty();
    delete a;
    vTaskDelete(nullptr);
}


// ════════════════════════════ v3: dances, groove, SD music ═════════════════
CoreS3Py32Led* Led() { return static_cast<CoreS3Py32Led*>(Board::GetInstance().GetLed()); }
void LightMood(const char* mood) {
    if (auto* l = Led()) l->ApplyLight("mood", "", mood, "rainbow", true, 80);
}
void LightNormal() {
    if (auto* l = Led()) l->ApplyLight("normal", "", "", "", false, 80);
}
void Face(const char* e) {
    if (g_display != nullptr) g_display->SetEmotion(e);
}
float HomePitch() { return g_servo != nullptr ? g_servo->GetHomePitch() : kLevelPitchDeg; }

// Built-in dances: keyframes of (ms, yaw deg, pitch deg ABOVE home/level).
struct Key { uint16_t t; int8_t yaw; int8_t dp; };
struct Preset { const char* name; const char* title; const char* mood; const char* face;
                const Key* keys; int n; };
const Key kWiggle[] = {{0,0,0},{250,-18,4},{500,18,4},{750,-18,4},{1000,18,4},{1250,0,0}};
const Key kNod[]    = {{0,0,0},{300,0,14},{600,0,2},{900,0,14},{1200,0,2},{1500,0,10},{1800,0,0}};
const Key kDisco[]  = {{0,0,0},{300,-30,18},{600,-30,2},{900,30,18},{1200,30,2},{1500,-15,10},
                       {1800,15,10},{2100,0,20},{2400,0,0}};
const Key kRobot[]  = {{0,0,0},{400,-35,0},{800,-35,12},{1200,0,12},{1600,35,12},{2000,35,0},{2400,0,0}};
const Key kSway[]   = {{0,0,2},{700,-22,6},{1400,22,6},{2100,-22,6},{2800,22,6},{3500,0,2}};
const Preset kPresets[] = {
    {"wiggle", "Wiggle", "party", "happy", kWiggle, (int)(sizeof(kWiggle) / sizeof(Key))},
    {"nod",    "Head bop", "happy", "happy", kNod, (int)(sizeof(kNod) / sizeof(Key))},
    {"disco",  "Disco", "party", "happy", kDisco, (int)(sizeof(kDisco) / sizeof(Key))},
    {"robot",  "Robot", "focus", "neutral", kRobot, (int)(sizeof(kRobot) / sizeof(Key))},
    {"sway",   "Slow sway", "calm", "loving", kSway, (int)(sizeof(kSway) / sizeof(Key))},
};
const Preset* FindPreset(const std::string& n) {
    for (const auto& p : kPresets) if (n == p.name || n == p.title) return &p;
    return nullptr;
}

struct PresetArgs { const Preset* p; int loops; };
std::atomic<int> g_preset_idx{-1};     // v3.5: which preset is dancing (-1 none)
std::atomic<bool> g_preset_task{false};
void PresetTask(void* arg) {
    auto* a = static_cast<PresetArgs*>(arg);
    g_preset_task.store(true);
    g_preset_idx.store((int)(a->p - kPresets));
    float home = HomePitch();
    LightMood(a->p->mood);
    UpdateParty();
    Face(a->p->face);
    for (int loop = 0; loop < a->loops && g_playing.load(); ++loop) {
        int64_t t0 = esp_timer_get_time() / 1000;
        for (int i = 0; i < a->p->n && g_playing.load(); ++i) {
            const Key& k = a->p->keys[i];
            int64_t due = t0 + k.t, now = esp_timer_get_time() / 1000;
            if (due > now) vTaskDelay(pdMS_TO_TICKS(due - now));
            if (g_servo != nullptr && g_servo->IsReady())
                g_servo->MoveTo((float)k.yaw, home + k.dp, 800);
        }
    }
    if (g_servo != nullptr && g_servo->IsReady()) g_servo->Center(400);
    LightNormal();
    Face("neutral");
    g_playing.store(false);
    g_preset_idx.store(-1);
    UpdateParty();
    g_preset_task.store(false);
    delete a;
    vTaskDelete(nullptr);
}

// ── dance to the music (beat follower on the played PCM loudness) ──
std::atomic<bool> g_groove{false};
std::atomic<int64_t> g_groove_until_ms{0};
bool GrooveSetting() { Settings s("hg", false); return s.GetInt("groove", 1) != 0; }
// v3.5: the groove task always runs during music (party face + beat lights);
// this only decides whether the head moves ("Dance to music" in Settings).
std::atomic<bool> g_groove_move{true};

bool DjActive();   // v3.6.7-dj, defined below

void GrooveTask(void*) {
    float fast = 0, slow = 0;
    int64_t last_beat = 0, last_sound = esp_timer_get_time() / 1000;
    int64_t started = last_sound;
    bool heard = false;
    int side = 1;
    float home = HomePitch();
    // v3.6.7-dj: looping DJ groove (continuous sway + bob locked to the beat)
    int beat_ms = 500;
    int64_t last_move = 0;
    float peak = 1500.0f;
    g_groove_move.store(GrooveSetting());
    LightMood("party");
    if (auto* l = Led()) l->SetBeatMode(true);
    UpdateParty();
    while (g_groove.load()) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now > g_groove_until_ms.load()) break;
        auto st = Application::GetInstance().GetDeviceState();
        if (st == kDeviceStateListening) break;          // she is listening to you
        uint16_t rms = 0;
        bool live = hg_meter::Sample(&rms, nullptr, 200);
        if (live && rms > 300) { last_sound = now; heard = true; }
        if (now - last_sound > (heard ? 3000 : 20000)) break;   // song over
        float x = live ? (float)rms : 0.0f;
        fast = 0.5f * fast + 0.5f * x;
        slow = 0.97f * slow + 0.03f * x;
        bool onset = fast > slow * 1.35f + 200.0f && now - last_beat > 280;
        peak = std::max(1200.0f, std::max(peak * 0.997f, fast));
        const bool dj = DjActive();
        if (onset && last_beat > 0) beat_ms = hg_djmove::UpdateBeat(beat_ms, now - last_beat);
        if (dj && g_groove_move.load() && !g_playing.load() && g_servo != nullptr &&
            g_servo->IsReady() && now - last_move >= hg_djmove::kMoveEveryMs) {
            last_move = now;
            hg_djmove::Pose p = hg_djmove::DjPose(now - started, beat_ms, fast / peak);
            g_servo->MoveTo(p.yaw, home + p.dpitch, hg_djmove::kServoSpeed);
        }
        if (onset) {
            last_beat = now;
            side = -side;
            if (auto* l = Led()) l->BeatPulse();
            if (!dj && g_groove_move.load() && !g_playing.load() && g_servo != nullptr &&
                g_servo->IsReady()) {
                float amp = std::min(22.0f, 8.0f + fast / 600.0f);
                g_servo->MoveTo(side * amp, home + ((now / 1000) % 2 ? 6.0f : 1.0f), 900);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    (void)started;
    if (g_groove_move.load() && g_servo != nullptr && g_servo->IsReady() && !g_playing.load())
        g_servo->Center(400);
    if (auto* l = Led()) l->SetBeatMode(false);
    LightNormal();
    g_groove.store(false);
    UpdateParty();
    vTaskDelete(nullptr);
}

// ── MP3 from /sd/music ──
constexpr const char* kMusicDir = "/sd/music";
std::atomic<bool> g_music{false};
std::string g_music_now;
std::mutex g_music_mutex;

std::function<void(bool)> g_party_hook;
std::mutex g_party_mutex;
bool g_party = false;
// v3.6.7-dj: DJ screen while a song plays and DJ mode is on (NVS hg/dj)
std::atomic<bool> g_dj_enabled{false};
std::function<void(bool)> g_dj_hook;
bool g_dj_shown = false;
std::string g_dj_track;            // title the server sent for its song
std::mutex g_dj_mutex;
std::atomic<bool> g_skip_busy{false};  // one server skip at a time (double taps)
std::function<bool(const std::string&, int)> g_dj_vu_hook;   // display: VU color / on / off / pulse
// DJ screen = DJ mode on AND a Mac (server) song is playing. SD music is not
// part of DJ mode (all DJ audio streams from the Mac music folder).
bool DjActive() { return g_dj_enabled.load() && g_groove.load(); }
void UpdateParty() {
    std::lock_guard<std::mutex> l(g_party_mutex);
    bool on = g_playing.load() || g_groove.load() || g_music.load();
    if (on != g_party) {
        g_party = on;
        if (g_party_hook) g_party_hook(on);
    }
    bool dj = DjActive();
    if (dj != g_dj_shown) {
        g_dj_shown = dj;
        if (g_dj_hook) g_dj_hook(dj);
    }
}

bool IsMp3(const char* n) {
    size_t l = strlen(n);
    return l > 4 && strcasecmp(n + l - 4, ".mp3") == 0 && n[0] != '.';
}

// Read up to cap bytes at offset. Holds the SD session only for this read.
long ReadChunk(const std::string& path, long off, uint8_t* dst, size_t cap, long* file_size) {
    SdSession s;
    if (!s.ok()) return -1;
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return -1;
    if (file_size != nullptr) { fseek(f, 0, SEEK_END); *file_size = ftell(f); }
    fseek(f, off, SEEK_SET);
    long n = (long)fread(dst, 1, cap, f);
    fclose(f);
    return n;
}

void MusicTask(void* arg) {
    std::string* path = static_cast<std::string*>(arg);
    constexpr size_t kChunk = 128 * 1024;     // ~8 s at 128 kbps per SD read
    constexpr size_t kTail = 8 * 1024;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(kChunk + kTail, MALLOC_CAP_SPIRAM);
    std::vector<uint8_t> pcm_raw(1152 * 2 * 2 * 2);
    void* dec = nullptr;
    esp_ae_rate_cvt_handle_t cvt = nullptr;
    int cvt_rate = 0;
    const int out_rate = Application::GetInstance().GetAudioService().OutputSampleRate();
    std::vector<int16_t> pending;
    pending.reserve(out_rate / 2);
    long file_off = 0, file_size = 0;
    size_t have = 0, pos = 0;
    bool eof = false;
    if (buf == nullptr || out_rate <= 0 || esp_mp3_dec_open(nullptr, 0, &dec) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "music: no memory/decoder");
        goto done;
    }
    LightMood("party");
    UpdateParty();
    if (!g_groove.load()) {   // v3.5: always (head moves only if the setting is on)
        g_groove_until_ms.store(esp_timer_get_time() / 1000 + 15 * 60 * 1000);
        g_groove.store(true);
        if (xTaskCreate(GrooveTask, "hg_groove", 3072, nullptr, 3, nullptr) != pdPASS) g_groove.store(false);
    }
    while (g_music.load()) {
        auto st = Application::GetInstance().GetDeviceState();
        if (st != kDeviceStateIdle) break;        // a conversation started: stop the song
        if (!eof && have - pos < kTail) {
            memmove(buf, buf + pos, have - pos);
            have -= pos; pos = 0;
            long n = ReadChunk(*path, file_off, buf + have, kChunk + kTail - have, file_off == 0 ? &file_size : nullptr);
            if (n <= 0) { eof = true; } else {
                if (file_off == 0 && n > 10 && memcmp(buf, "ID3", 3) == 0) {   // skip ID3v2 tag
                    long tag = 10 + ((buf[6] & 0x7f) << 21 | (buf[7] & 0x7f) << 14 |
                                     (buf[8] & 0x7f) << 7 | (buf[9] & 0x7f));
                    if (tag < n) { pos = tag; } else { file_off = tag; continue; }
                }
                have += n; file_off += n;
                if (file_size > 0 && file_off >= file_size) eof = true;
            }
        }
        if (pos >= have) break;
        esp_audio_dec_in_raw_t raw = {};
        raw.buffer = buf + pos;
        raw.len = (uint32_t)(have - pos);
        esp_audio_dec_out_frame_t out = {};
        out.buffer = pcm_raw.data();
        out.len = (uint32_t)pcm_raw.size();
        esp_audio_dec_info_t info = {};
        esp_audio_err_t r = esp_mp3_dec_decode(dec, &raw, &out, &info);
        if (r == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && out.needed_size > out.len) {
            pcm_raw.resize(out.needed_size);
            continue;
        }
        pos += raw.consumed > 0 ? raw.consumed : 1;
        if (r != ESP_AUDIO_ERR_OK || out.decoded_size == 0 || info.sample_rate == 0) continue;
        int ch = info.channel > 0 ? info.channel : 1;
        size_t frames = out.decoded_size / 2 / ch;
        const int16_t* s16 = (const int16_t*)pcm_raw.data();
        std::vector<int16_t> mono(frames);
        for (size_t i = 0; i < frames; ++i) {
            int acc = 0;
            for (int c = 0; c < ch; ++c) acc += s16[i * ch + c];
            mono[i] = (int16_t)(acc / ch);
        }
        if ((int)info.sample_rate != out_rate) {
            if (cvt == nullptr || cvt_rate != (int)info.sample_rate) {
                if (cvt) esp_ae_rate_cvt_close(cvt);
                cvt = nullptr;
                esp_ae_rate_cvt_cfg_t cfg = {};
                cfg.src_rate = info.sample_rate; cfg.dest_rate = (uint32_t)out_rate;
                cfg.channel = 1; cfg.bits_per_sample = ESP_AUDIO_BIT16; cfg.complexity = 2;
                cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                esp_ae_rate_cvt_open(&cfg, &cvt);
                cvt_rate = info.sample_rate;
            }
            if (cvt == nullptr) break;
            uint32_t max_out = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(cvt, mono.size(), &max_out);
            std::vector<int16_t> rs(max_out);
            uint32_t got = max_out;
            esp_ae_rate_cvt_process(cvt, (esp_ae_sample_t)mono.data(), mono.size(),
                                    (esp_ae_sample_t)rs.data(), &got);
            pending.insert(pending.end(), rs.begin(), rs.begin() + got);
        } else {
            pending.insert(pending.end(), mono.begin(), mono.end());
        }
        // push ~240 ms at a time so an SD read (~0.2-0.4 s) never starves the speaker
        if ((int)pending.size() >= out_rate * 240 / 1000) {
            if (!Application::GetInstance().GetAudioService().PushPcmToPlayback(std::move(pending))) break;
            pending = std::vector<int16_t>();
            pending.reserve(out_rate / 2);
        }
    }
    if (!pending.empty() && g_music.load())
        Application::GetInstance().GetAudioService().PushPcmToPlayback(std::move(pending));
done:
    if (cvt) esp_ae_rate_cvt_close(cvt);
    if (dec) esp_mp3_dec_close(dec);
    if (buf) heap_caps_free(buf);
    delete path;
    {
        std::lock_guard<std::mutex> l(g_music_mutex);
        g_music_now.clear();
    }
    g_music.store(false);
    LightNormal();
    UpdateParty();
    ESP_LOGI(TAG, "music task done");
    vTaskDelete(nullptr);
}

}  // namespace

void ParkSdChipSelect() {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << kSdCs;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    gpio_set_level(kSdCs, 1);
}

// ════════════════════════════ v3 public API (Settings screen + tools) ══════
std::string SdStatusText() {
    SdSession s;
    if (!s.ok()) return "No SD card (" + s.error() + ")";
    char b[96];
    snprintf(b, sizeof(b), "SD card OK, %.1f GB", s.CardBytes() / 1e9);
    return b;
}

std::vector<std::string> ListMusic(int max) {
    std::vector<std::string> out;
    SdSession s;
    if (!s.ok()) return out;
    mkdir(kMusicDir, 0775);
    DIR* d = opendir(kMusicDir);
    if (d == nullptr) return out;
    while (dirent* e = readdir(d)) {
        if (IsMp3(e->d_name)) out.push_back(e->d_name);
        if ((int)out.size() >= max) break;
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool PlaySdMusic(const std::string& name, std::string* why) {
    if (g_music.load()) StopSdMusic();
    for (int i = 0; i < 50 && g_music.load(); ++i) vTaskDelay(pdMS_TO_TICKS(20));
    auto tracks = ListMusic(200);
    if (tracks.empty()) { *why = "no MP3 files in /music on the SD card"; return false; }
    std::string pick;
    if (name.empty() || name == "random" || name == "any") {
        pick = tracks[esp_random() % tracks.size()];
    } else {
        std::string want = name;
        std::transform(want.begin(), want.end(), want.begin(), ::tolower);
        for (const auto& t : tracks) {
            std::string low = t;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (low == want || low == want + ".mp3" || low.find(want) != std::string::npos) { pick = t; break; }
        }
        if (pick.empty()) { *why = "no track matching '" + name + "'"; return false; }
    }
    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
        *why = "busy talking; try again when she is idle";
        return false;
    }
    {
        std::lock_guard<std::mutex> l(g_music_mutex);
        g_music_now = pick;
    }
    g_music.store(true);
    auto* path = new std::string(std::string(kMusicDir) + "/" + pick);
    if (xTaskCreatePinnedToCore(MusicTask, "hg_music", 6144, path, 4, nullptr, 1) != pdPASS) {
        delete path; g_music.store(false);
        *why = "task start failed";
        return false;
    }
    return true;
}

void StopSdMusic() { g_music.store(false); }
bool SdMusicPlaying() { return g_music.load(); }
std::string SdMusicNow() { std::lock_guard<std::mutex> l(g_music_mutex); return g_music_now; }

std::vector<std::string> PresetNames() {
    std::vector<std::string> v;
    for (const auto& p : kPresets) v.push_back(p.name);
    return v;
}

bool PlayPreset(const std::string& name, int loops, std::string* why) {
    const Preset* p = FindPreset(name);
    if (p == nullptr) { *why = "unknown dance; try wiggle, nod, disco, robot or sway"; return false; }
    if (g_servo == nullptr || !g_servo->IsReady()) { *why = "servos not ready"; return false; }
    if (g_recording.load() || g_playing.load()) { *why = "busy"; return false; }
    g_playing.store(true);
    auto* a = new PresetArgs{p, loops < 1 ? 1 : (loops > 10 ? 10 : loops)};
    if (xTaskCreate(PresetTask, "hg_preset", 3072, a, 3, nullptr) != pdPASS) {
        delete a; g_playing.store(false); *why = "task start failed"; return false;
    }
    return true;
}

bool StartRecording(std::string* name_out, std::string* why) {
    if (g_servo == nullptr || !g_servo->IsReady()) { *why = "servos not ready"; return false; }
    if (g_recording.load() || g_playing.load()) { *why = "busy"; return false; }
    char nm[24];
    snprintf(nm, sizeof(nm), "my_dance_%lu", (unsigned long)((esp_timer_get_time() / 1000000) % 100000));
    {
        std::lock_guard<std::mutex> l(g_rec_mutex);
        g_rec.clear();
        g_rec.reserve(kMaxDanceRows);
    }
    g_rec_name = nm;
    g_rec_sample_ms = 100;
    g_rec_max_s = 60;
    g_recording.store(true);
    if (xTaskCreate(RecordTask, "hg_dance_rec", 3072, nullptr, 3, nullptr) != pdPASS) {
        g_recording.store(false); *why = "task start failed"; return false;
    }
    *name_out = nm;
    return true;
}

bool StopRecording(std::string* path_out, std::string* why) {
    g_recording.store(false);
    vTaskDelay(pdMS_TO_TICKS(g_rec_sample_ms + 50));
    std::vector<DanceRow> rows;
    {
        std::lock_guard<std::mutex> l(g_rec_mutex);
        rows.swap(g_rec);
    }
    if (rows.empty()) { *why = "nothing recorded"; return false; }
    std::string csv = DanceToCsv(rows, g_rec_sample_ms);
    SdSession s;
    if (!s.ok()) { *why = s.error(); return false; }
    std::string full = std::string(kRoot) + "/dances/" + g_rec_name + ".csv";
    if (!WriteFile(full, csv, false)) { *why = "write failed"; return false; }
    *path_out = full;
    return true;
}

bool IsRecording() { return g_recording.load(); }
bool IsDancing() { return g_playing.load() || g_groove.load(); }

std::vector<std::string> ListDances() {
    std::vector<std::string> out;
    SdSession s;
    if (!s.ok()) return out;
    DIR* d = opendir((std::string(kRoot) + "/dances").c_str());
    if (d == nullptr) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".csv") == 0) out.push_back(n.substr(0, n.size() - 4));
        if (out.size() >= 20) break;
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool PlaySavedDance(const std::string& name, std::string* why) {
    std::string data;
    if (!SafeId(name)) { *why = "bad name"; return false; }
    if (g_servo == nullptr || !g_servo->IsReady()) { *why = "servos not ready"; return false; }
    if (g_recording.load() || g_playing.load()) { *why = "busy"; return false; }
    {
        SdSession s;
        if (!s.ok()) { *why = s.error(); return false; }
        if (!ReadFile(std::string(kRoot) + "/dances/" + name + ".csv", 64 * 1024, &data, why)) return false;
    }
    auto* a = new PlayArgs();
    if (!CsvToDance(data, &a->rows)) { delete a; *why = "empty dance"; return false; }
    a->speed = 1.0f;
    a->loops = 1;
    g_playing.store(true);
    if (xTaskCreate(PlayTask, "hg_dance_play", 3072, a, 3, nullptr) != pdPASS) {
        g_playing.store(false); delete a; *why = "task start failed"; return false;
    }
    return true;
}

void StopDance() { g_playing.store(false); g_recording.store(false); g_groove.store(false); }

bool GrooveEnabled() { return GrooveSetting(); }
void SetGrooveEnabled(bool on) {
    Settings s("hg", true);
    s.SetInt("groove", on ? 1 : 0);
    g_groove_move.store(on);   // v3.5: the party face keeps going, the head stops
}

void SetPartyHook(std::function<void(bool)> hook) {
    std::lock_guard<std::mutex> l(g_party_mutex);
    g_party_hook = std::move(hook);
}
bool IsParty() { return g_playing.load() || g_groove.load() || g_music.load(); }

// ── v3.5 head swipes ──
namespace {
void ServerSkipTask(void* arg) {
    // Stop the current server song, then ask the server for the next or
    // previous one the same way the spoken phrase would. v3.6.7-dj: the server
    // keeps a real play history (play_music.py PlayHistory), so "next song" /
    // "previous song" step through a real order. Needs the v3.6.7-dj server pack.
    const char* phrase = static_cast<const char*>(arg);
    auto& app = Application::GetInstance();
    bool asked = false;
    for (int i = 0; i < 80 && !asked; ++i) {          // up to 8 s
        auto st = app.GetDeviceState();
        if (st == kDeviceStateSpeaking) {
            if (i == 0) app.AbortSpeaking(kAbortReasonNone);
        } else if (st == kDeviceStateListening) {
            app.WakeWordInvoke("");                     // closes the open turn
            vTaskDelay(pdMS_TO_TICKS(400));
        } else if (st == kDeviceStateIdle) {
            vTaskDelay(pdMS_TO_TICKS(300));
            app.WakeWordInvoke(phrase);
            asked = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "skip: server '%s' %s", phrase, asked ? "requested" : "gave up");
    g_skip_busy.store(false);
    vTaskDelete(nullptr);
}
const char kNextPhrase[] = "next song";
const char kPrevPhrase[] = "previous song";

void NextPresetTask(void* arg) {
    int next = (int)(intptr_t)arg;
    g_playing.store(false);
    for (int i = 0; i < 60 && g_preset_task.load(); ++i) vTaskDelay(pdMS_TO_TICKS(50));
    std::string why;
    if (!PlayPreset(kPresets[next].name, 2, &why)) ESP_LOGW(TAG, "next dance: %s", why.c_str());
    vTaskDelete(nullptr);
}

void NextSdSongTask(void*) {
    std::string now = SdMusicNow();
    auto tracks = ListMusic(200);
    std::string pick;
    if (!tracks.empty()) {
        size_t i = 0;
        while (i < tracks.size() && tracks[i] != now) ++i;
        pick = tracks[(i + 1) % tracks.size()];
    }
    std::string why;
    if (pick.empty() || !PlaySdMusic(pick, &why)) ESP_LOGW(TAG, "next song: %s", why.c_str());
    vTaskDelete(nullptr);
}
}  // namespace

void SwipeNext() {
    auto st = Application::GetInstance().GetDeviceState();
    if (g_music.load()) {
        ESP_LOGI(TAG, "swipe: next SD song");
        xTaskCreate(NextSdSongTask, "hg_next", 4096, nullptr, 3, nullptr);
    } else if (g_groove.load() && st == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "swipe: next server song");
        if (!g_skip_busy.exchange(true) &&
            xTaskCreate(ServerSkipTask, "hg_next", 4096, (void*)kNextPhrase, 3, nullptr) != pdPASS)
            g_skip_busy.store(false);
    } else if (g_preset_idx.load() >= 0) {
        int n = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
        int next = (g_preset_idx.load() + 1) % n;
        ESP_LOGI(TAG, "swipe: next dance %s", kPresets[next].name);
        xTaskCreate(NextPresetTask, "hg_next", 3072, (void*)(intptr_t)next, 3, nullptr);
    } else if (!g_playing.load() && !g_recording.load() && st == kDeviceStateIdle) {
        std::string why;
        ESP_LOGI(TAG, "swipe: wiggle");
        PlayPreset("wiggle", 1, &why);
    }
}

// v3.6.7-dj: previous song (screen swipe right, DJ prev button). Mac (server)
// songs only: the server keeps the history / the set list order. SD music is
// unchanged from v3.6.6 (no previous there).
void SwipePrev() {
    auto st = Application::GetInstance().GetDeviceState();
    if (g_groove.load() && st == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "skip: previous server song");
        if (!g_skip_busy.exchange(true) &&
            xTaskCreate(ServerSkipTask, "hg_prev", 4096, (void*)kPrevPhrase, 3, nullptr) != pdPASS)
            g_skip_busy.store(false);
    }
}

bool MusicPlaying() {
    return g_music.load() ||
        (g_groove.load() && Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking);
}

bool DjEnabled() { return g_dj_enabled.load(); }
void SetDjEnabled(bool on) {
    Settings s("hg", true);
    s.SetInt("dj", on ? 1 : 0);
    g_dj_enabled.store(on);
    UpdateParty();
}
bool DjShown() { return DjActive(); }
void SetDjHook(std::function<void(bool)> hook) {
    std::lock_guard<std::mutex> l(g_party_mutex);
    g_dj_hook = std::move(hook);
}
void SetDjTrack(const std::string& t) {
    std::lock_guard<std::mutex> l(g_dj_mutex);
    g_dj_track = t.substr(0, 80);
}
std::string DjTitle() {
    std::lock_guard<std::mutex> l(g_dj_mutex);
    return g_dj_track;
}
void SetDjVuHook(std::function<bool(const std::string&, int)> hook) {
    std::lock_guard<std::mutex> l(g_party_mutex);
    g_dj_vu_hook = std::move(hook);
}
// v3.6.7-dj4: rave mode (DJ screen background follows the music). Session
// only: never saved, every boot starts with it off.
static std::atomic<bool> g_dj_rave{false};
static std::function<void(bool)> g_dj_rave_hook;
bool DjRave() { return g_dj_rave.load(); }
void SetDjRave(bool on) {
    g_dj_rave.store(on);
    std::function<void(bool)> hook;
    {
        std::lock_guard<std::mutex> l(g_party_mutex);
        hook = g_dj_rave_hook;
    }
    if (hook) hook(on);
}
void SetDjRaveHook(std::function<void(bool)> hook) {
    std::lock_guard<std::mutex> l(g_party_mutex);
    g_dj_rave_hook = std::move(hook);
}

void SwipeStop() {
    bool server_song = g_groove.load() &&
        Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking;
    ESP_LOGI(TAG, "swipe: stop%s", server_song ? " (server song)" : "");
    if (g_music.load()) StopSdMusic();
    if (g_playing.load() || g_groove.load()) {
        g_playing.store(false);
        g_groove.store(false);
    }
    if (server_song) {
        Application::GetInstance().Schedule([]() {
            Application::GetInstance().AbortSpeaking(kAbortReasonNone);
        });
    }
}

bool ArmGroove(int max_seconds) {
    g_groove_until_ms.store(esp_timer_get_time() / 1000 + (int64_t)max_seconds * 1000);
    if (g_groove.exchange(true)) return true;
    if (xTaskCreate(GrooveTask, "hg_groove", 3072, nullptr, 3, nullptr) != pdPASS) {
        g_groove.store(false);
        return false;
    }
    return true;
}

bool CameraEnabled() { return HgCameraEnabled().load(); }
void SetCameraEnabled(bool on) {
    HgCameraEnabled().store(on);
    Settings s("hg", true);
    s.SetInt("camera", on ? 1 : 0);
}

void RegisterTools(Display* display, Camera* camera, CoreS3Servo* servo) {
    g_display = display;
    g_camera = camera;
    g_servo = servo;
    auto& mcp = McpServer::GetInstance();

    // ── face ──
    mcp.AddTool("self.face.show",
        "Show one of the robot's face animations on screen. emotion: neutral, happy, sad, "
        "thinking, sleeping, pet, shake, speaking, listening, look_left, look_right, email, "
        "scowl (a fierce angry look). "
        "Use email while sending an email and happy when it was sent.",
        PropertyList({Property("emotion", kPropertyTypeString, std::string("neutral"))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string e = p["emotion"].value<std::string>();
            if (!SafeId(e)) return Err("bad emotion name");
            if (g_display) g_display->SetEmotion(e.c_str());
            return std::string("{\"ok\":true}");
        });

    // ── SD basics ──
    mcp.AddTool("self.sd.status",
        "Check whether a microSD card is inserted and readable. Files live under /sd/agent.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            SdSession s;
            if (!s.ok()) return Err(s.error());
            cJSON* o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "card_mb", (double)(s.CardBytes() / (1024 * 1024)));
            cJSON_AddStringToObject(o, "root", kRoot);
            return OkJson(o);
        });

    mcp.AddTool("self.sd.list",
        "List files in a folder under /sd/agent (path is relative, empty = top). "
        "Folders: photos, notes, profiles, faces, dances, wakewords, music.",
        PropertyList({Property("path", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string full, why;
            if (!SafeRel(p["path"].value<std::string>(), &full, &why)) return Err(why);
            SdSession s;
            if (!s.ok()) return Err(s.error());
            cJSON* o = cJSON_CreateObject();
            cJSON_AddItemToObject(o, "items", ListDir(full));
            return OkJson(o);
        });

    mcp.AddTool("self.sd.read_text",
        "Read a small text note (.txt .json .md .csv, up to 8 KB) under /sd/agent, "
        "for example notes/shopping.txt.",
        PropertyList({Property("path", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string rel = p["path"].value<std::string>(), full, why, data;
            if (!SafeRel(rel, &full, &why)) return Err(why);
            if (!TextExtOk(rel)) return Err("only .txt .json .md .csv");
            SdSession s;
            if (!s.ok()) return Err(s.error());
            if (!ReadFile(full, kMaxText, &data, &why)) return Err(why);
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "content", data.c_str());
            return OkJson(o);
        });

    mcp.AddTool("self.sd.write_text",
        "Save a small text note (.txt .json .md .csv, up to 8 KB) under /sd/agent, "
        "for example notes/today.txt. append=true adds to the end.",
        PropertyList({Property("path", kPropertyTypeString, std::string("")),
                      Property("content", kPropertyTypeString, std::string("")),
                      Property("append", kPropertyTypeBoolean, false)}),
        [](const PropertyList& p) -> ReturnValue {
            std::string rel = p["path"].value<std::string>(), full, why;
            std::string content = p["content"].value<std::string>();
            if (!SafeRel(rel, &full, &why)) return Err(why);
            if (!TextExtOk(rel)) return Err("only .txt .json .md .csv");
            if (content.size() > kMaxText) return Err("content over 8 KB");
            SdSession s;
            if (!s.ok()) return Err(s.error());
            if (!WriteFile(full, content, p["append"].value<bool>())) return Err("write failed");
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "path", full.c_str());
            return OkJson(o);
        });

    mcp.AddTool("self.sd.save_photo",
        "Take a photo with the camera and save it as a JPEG in /sd/agent/photos. "
        "name is optional (letters, digits, _ -). Returns the saved path.",
        PropertyList({Property("name", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            if (g_camera == nullptr) return Err("no camera");
            std::string name = p["name"].value<std::string>();
            if (name.empty()) name = "photo_" + std::to_string(esp_timer_get_time() / 1000000);
            if (!SafeId(name)) return Err("bad name");
            std::string jpeg;
            // capture BEFORE taking the SD session: capture draws a preview
            if (!g_camera->CaptureJpeg(jpeg, 80)) return Err("capture failed");
            SdSession s;
            if (!s.ok()) return Err(s.error());
            std::string full = std::string(kRoot) + "/photos/" + name + ".jpg";
            if (!WriteFile(full, jpeg, false)) return Err("write failed");
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "path", full.c_str());
            cJSON_AddNumberToObject(o, "bytes", (double)jpeg.size());
            return OkJson(o);
        });

    // ── profiles (/sd/agent/profiles/<id>.json) ──
    mcp.AddTool("self.profile.save",
        "Save a person's profile to /sd/agent/profiles/<id>.json during setup. profile_json "
        "must be a JSON object with schema hg.profile.v1: id, name, face_id, "
        "personality {style, voice, notes}, created, updated. Up to 4 KB.",
        PropertyList({Property("id", kPropertyTypeString, std::string("")),
                      Property("profile_json", kPropertyTypeString, std::string("{}"))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string id = p["id"].value<std::string>();
            std::string js = p["profile_json"].value<std::string>();
            if (!SafeId(id)) return Err("bad id");
            if (js.size() > kMaxProfile) return Err("profile over 4 KB");
            cJSON* root = cJSON_Parse(js.c_str());
            if (!cJSON_IsObject(root)) { cJSON_Delete(root); return Err("not a JSON object"); }
            cJSON* jid = cJSON_GetObjectItem(root, "id");
            cJSON* jname = cJSON_GetObjectItem(root, "name");
            bool valid = cJSON_IsString(jid) && id == jid->valuestring && cJSON_IsString(jname);
            if (valid && !cJSON_GetObjectItem(root, "schema"))
                cJSON_AddStringToObject(root, "schema", "hg.profile.v1");
            char* out = valid ? cJSON_PrintUnformatted(root) : nullptr;
            cJSON_Delete(root);
            if (!valid) return Err("profile needs id (matching) and name");
            std::string data = out; cJSON_free(out);
            SdSession s;
            if (!s.ok()) return Err(s.error());
            std::string full = std::string(kRoot) + "/profiles/" + id + ".json";
            if (!WriteFile(full, data, false)) return Err("write failed");
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "path", full.c_str());
            return OkJson(o);
        });

    mcp.AddTool("self.profile.load",
        "Load a saved person profile by id from /sd/agent/profiles.",
        PropertyList({Property("id", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string id = p["id"].value<std::string>(), data, why;
            if (!SafeId(id)) return Err("bad id");
            SdSession s;
            if (!s.ok()) return Err(s.error());
            if (!ReadFile(std::string(kRoot) + "/profiles/" + id + ".json", kMaxProfile, &data, &why))
                return Err(why);
            cJSON* o = cJSON_CreateObject();
            cJSON_AddItemToObject(o, "profile", cJSON_Parse(data.c_str()));
            return OkJson(o);
        });

    mcp.AddTool("self.profile.list",
        "List saved person profiles on the SD card.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            SdSession s;
            if (!s.ok()) return Err(s.error());
            cJSON* o = cJSON_CreateObject();
            cJSON_AddItemToObject(o, "items", ListDir(std::string(kRoot) + "/profiles"));
            return OkJson(o);
        });

    // ── face cache (/sd/agent/faces/<face_id>.json, embeddings only) ──
    mcp.AddTool("self.face_cache.save",
        "Store a face embedding (numbers only, no photo) for face_id in /sd/agent/faces. "
        "embedding_json: {\"model\":\"...\",\"dim\":512,\"vectors\":[[...]]}. Up to 16 KB.",
        PropertyList({Property("face_id", kPropertyTypeString, std::string("")),
                      Property("embedding_json", kPropertyTypeString, std::string("{}"))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string id = p["face_id"].value<std::string>();
            std::string js = p["embedding_json"].value<std::string>();
            if (!SafeId(id)) return Err("bad face_id");
            if (js.size() > kMaxFace) return Err("embedding over 16 KB");
            cJSON* root = cJSON_Parse(js.c_str());
            bool valid = cJSON_IsObject(root) && cJSON_IsArray(cJSON_GetObjectItem(root, "vectors"));
            cJSON_Delete(root);
            if (!valid) return Err("needs a vectors array");
            SdSession s;
            if (!s.ok()) return Err(s.error());
            std::string full = std::string(kRoot) + "/faces/" + id + ".json";
            if (!WriteFile(full, js, false)) return Err("write failed");
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "path", full.c_str());
            return OkJson(o);
        });

    mcp.AddTool("self.face_cache.load",
        "Load a stored face embedding by face_id from /sd/agent/faces.",
        PropertyList({Property("face_id", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string id = p["face_id"].value<std::string>(), data, why;
            if (!SafeId(id)) return Err("bad face_id");
            SdSession s;
            if (!s.ok()) return Err(s.error());
            if (!ReadFile(std::string(kRoot) + "/faces/" + id + ".json", kMaxFace, &data, &why))
                return Err(why);
            cJSON* o = cJSON_CreateObject();
            cJSON_AddItemToObject(o, "embedding", cJSON_Parse(data.c_str()));
            return OkJson(o);
        });

    mcp.AddTool("self.face_cache.list",
        "List face_ids that have stored embeddings.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            SdSession s;
            if (!s.ok()) return Err(s.error());
            cJSON* o = cJSON_CreateObject();
            cJSON_AddItemToObject(o, "items", ListDir(std::string(kRoot) + "/faces"));
            return OkJson(o);
        });

    // ── dances (/sd/agent/dances/<name>.csv) ──
    mcp.AddTool("self.dance.record_start",
        "Start recording a head dance. Move her head with the K151 remote joystick; "
        "head angles are sampled every sample_ms until self.dance.record_stop (max max_seconds).",
        PropertyList({Property("name", kPropertyTypeString, std::string("dance1")),
                      Property("sample_ms", kPropertyTypeInteger, 100, 50, 500),
                      Property("max_seconds", kPropertyTypeInteger, 60, 5, 120)}),
        [](const PropertyList& p) -> ReturnValue {
            std::string name = p["name"].value<std::string>();
            if (!SafeId(name)) return Err("bad name");
            if (g_servo == nullptr || !g_servo->IsReady()) return Err("servos not ready");
            if (g_recording.load() || g_playing.load()) return Err("busy");
            {
                std::lock_guard<std::mutex> l(g_rec_mutex);
                g_rec.clear();
                g_rec.reserve(kMaxDanceRows);
            }
            g_rec_name = name;
            g_rec_sample_ms = p["sample_ms"].value<int>();
            g_rec_max_s = p["max_seconds"].value<int>();
            g_recording.store(true);
            if (xTaskCreate(RecordTask, "hg_dance_rec", 3072, nullptr, 3, nullptr) != pdPASS) {
                g_recording.store(false);
                return Err("task start failed");
            }
            return std::string("{\"ok\":true,\"recording\":true}");
        });

    mcp.AddTool("self.dance.record_stop",
        "Stop recording and save the dance to /sd/agent/dances/<name>.csv.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            g_recording.store(false);
            vTaskDelay(pdMS_TO_TICKS(g_rec_sample_ms + 50));
            std::vector<DanceRow> rows;
            {
                std::lock_guard<std::mutex> l(g_rec_mutex);
                rows.swap(g_rec);
            }
            if (rows.empty()) return Err("nothing recorded");
            std::string csv = DanceToCsv(rows, g_rec_sample_ms);
            SdSession s;
            if (!s.ok()) return Err(s.error());
            std::string full = std::string(kRoot) + "/dances/" + g_rec_name + ".csv";
            if (!WriteFile(full, csv, false)) return Err("write failed");
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "path", full.c_str());
            cJSON_AddNumberToObject(o, "samples", (double)rows.size());
            return OkJson(o);
        });

    mcp.AddTool("self.dance.play",
        "Play a saved head dance from /sd/agent/dances. speed 50-200 (percent), loops 1-10. "
        "To dance to music, call this right after starting the song.",
        PropertyList({Property("name", kPropertyTypeString, std::string("dance1")),
                      Property("speed", kPropertyTypeInteger, 100, 50, 200),
                      Property("loops", kPropertyTypeInteger, 1, 1, 10)}),
        [](const PropertyList& p) -> ReturnValue {
            std::string name = p["name"].value<std::string>(), data, why;
            if (!SafeId(name)) return Err("bad name");
            if (g_servo == nullptr || !g_servo->IsReady()) return Err("servos not ready");
            if (g_recording.load() || g_playing.load()) return Err("busy");
            {
                SdSession s;
                if (!s.ok()) return Err(s.error());
                if (!ReadFile(std::string(kRoot) + "/dances/" + name + ".csv", 64 * 1024, &data, &why))
                    return Err(why);
            }
            auto* a = new PlayArgs();
            if (!CsvToDance(data, &a->rows)) { delete a; return Err("empty dance"); }
            a->speed = p["speed"].value<int>() / 100.0f;
            a->loops = p["loops"].value<int>();
            g_playing.store(true);
            if (xTaskCreate(PlayTask, "hg_dance_play", 3072, a, 3, nullptr) != pdPASS) {
                g_playing.store(false); delete a;
                return Err("task start failed");
            }
            return std::string("{\"ok\":true,\"playing\":true}");
        });

    mcp.AddTool("self.dance.stop",
        "Stop a dance that is playing or being recorded.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            g_playing.store(false);
            g_recording.store(false);
            return std::string("{\"ok\":true}");
        });

    mcp.AddTool("self.dance.list",
        "List saved dances on the SD card.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            SdSession s;
            if (!s.ok()) return Err(s.error());
            cJSON* o = cJSON_CreateObject();
            cJSON_AddItemToObject(o, "items", ListDir(std::string(kRoot) + "/dances"));
            return OkJson(o);
        });


    // ── v3 tools ──
    {
        Settings cs("hg", false);
        HgCameraEnabled().store(cs.GetInt("camera", 1) != 0);
    }
    mcp.AddTool("self.dance.preset",
        "Do a built-in dance with head moves, lights and face. Use for 'dance', 'do a dance', "
        "'show me a dance'. name: wiggle | nod | disco | robot | sway (pick one if the user "
        "didn't say). loops 1-10. To learn a new dance from the remote joystick use "
        "self.dance.record_start; to replay one the user taught her use self.dance.play.",
        PropertyList({Property("name", kPropertyTypeString, std::string("disco")),
                      Property("loops", kPropertyTypeInteger, 2, 1, 10)}),
        [](const PropertyList& p) -> ReturnValue {
            std::string why;
            if (!PlayPreset(p["name"].value<std::string>(), p["loops"].value<int>(), &why)) return Err(why);
            return std::string("{\"ok\":true,\"dancing\":true}");
        });

    mcp.AddTool("self.dance.groove",
        "Dance to the music that is playing (follows the beat with head and lights) and stop "
        "by itself when the song ends. on=false stops. The server calls this when it plays a song.",
        PropertyList({Property("on", kPropertyTypeBoolean, true),
                      Property("max_seconds", kPropertyTypeInteger, 600, 10, 1800)}),
        [](const PropertyList& p) -> ReturnValue {
            if (!p["on"].value<bool>()) { g_groove.store(false); return std::string("{\"ok\":true,\"groove\":false}"); }
            if (!ArmGroove(p["max_seconds"].value<int>()))
                return Err("dance-to-music is turned off in Settings");
            return std::string("{\"ok\":true,\"groove\":true}");
        });

    g_dj_enabled.store(Settings("hg", false).GetInt("dj", 0) != 0);
    mcp.AddTool("self.dj.mode",
        "DJ mode: while a Mac (server) song plays she shows the DJ screen (her face "
        "over a 13-band equalizer that moves to the music) and grooves to the beat. action: on | off | "
        "status | track | rave_on | rave_off. on/off are saved (same as Settings > Music > DJ mode). "
        "track: the title of the server song now playing (the server sends it). rave_on / rave_off: "
        "rave mode, the whole DJ screen background changes color with the music (smooth, at most 3 "
        "pulses a second); for 'rave mode', 'party lights on the screen'. Rave is not saved and is "
        "off after a restart; rave_on also turns DJ mode on.",
        PropertyList({Property("action", kPropertyTypeString, std::string("status")),
                      Property("track", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string a = p["action"].value<std::string>();
            if (a == "on" || a == "off") SetDjEnabled(a == "on");
            else if (a == "track") SetDjTrack(p["track"].value<std::string>());
            else if (a == "rave_on") { if (!DjEnabled()) SetDjEnabled(true); SetDjRave(true); }   // v3.6.7-dj4
            else if (a == "rave_off") SetDjRave(false);
            else if (a != "status") return Err("action must be on, off, status, track, rave_on or rave_off");
            cJSON* o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "dj", DjEnabled());
            cJSON_AddBoolToObject(o, "showing", DjActive());
            cJSON_AddBoolToObject(o, "rave", DjRave());
            cJSON_AddStringToObject(o, "track", DjTitle().c_str());
            return OkJson(o);
        });

    mcp.AddTool("self.dj.vu",
        "DJ screen equalizer (13 columns along the bottom, driven by the live audio; runs on its own, "
        "colors turn slowly and flash on the beat, her LEDs follow). A color picks where the palette "
        "starts; it keeps turning. "
        "mode: purple | blue | gold | warm | rainbow | classic (color), on | off, or pulse "
        "(a bright pulse for seconds, 1-30).",
        PropertyList({Property("mode", kPropertyTypeString, std::string("on")),
                      Property("seconds", kPropertyTypeInteger, 2, 0, 30)}),
        [](const PropertyList& p) -> ReturnValue {
            std::string m = p["mode"].value<std::string>();
            int secs = p["seconds"].value<int>();
            if (hg_vu::ParseMode(m).kind == hg_vu::ModeKind::kBad)
                return Err("mode must be purple, blue, gold, warm, rainbow, classic, on, off or pulse");
            std::function<bool(const std::string&, int)> hook;
            {
                std::lock_guard<std::mutex> l(g_party_mutex);
                hook = g_dj_vu_hook;
            }
            bool ok = hook && hook(m, secs * 1000);
            cJSON* o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "applied", ok);
            cJSON_AddBoolToObject(o, "showing", DjActive());
            return OkJson(o);
        });

    mcp.AddTool("self.music.sd",
        "Music stored on her own microSD card (folder /music, MP3). action: play | stop | list | "
        "now. track: part of a file name, or empty for a random song. She dances along if "
        "'Dance to music' is on. Songs on the Mac server use the server's play_music instead.",
        PropertyList({Property("action", kPropertyTypeString, std::string("play")),
                      Property("track", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            std::string a = p["action"].value<std::string>();
            if (a == "stop") { StopSdMusic(); return std::string("{\"ok\":true}"); }
            if (a == "now") {
                cJSON* o = cJSON_CreateObject();
                cJSON_AddBoolToObject(o, "playing", SdMusicPlaying());
                cJSON_AddStringToObject(o, "track", SdMusicNow().c_str());
                return OkJson(o);
            }
            if (a == "list") {
                cJSON* o = cJSON_CreateObject();
                cJSON* arr = cJSON_CreateArray();
                for (const auto& t : ListMusic(50)) cJSON_AddItemToArray(arr, cJSON_CreateString(t.c_str()));
                cJSON_AddItemToObject(o, "tracks", arr);
                return OkJson(o);
            }
            std::string why;
            // play after the reply finishes: she must be idle to play local music
            std::string track = p["track"].value<std::string>();
            Application::GetInstance().Schedule([track]() {
                xTaskCreate([](void* arg) {
                    auto* t = static_cast<std::string*>(arg);
                    for (int i = 0; i < 300; ++i) {      // wait up to 30 s for idle
                        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) break;
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    vTaskDelay(pdMS_TO_TICKS(400));
                    std::string w;
                    if (!PlaySdMusic(*t, &w)) ESP_LOGW(TAG, "sd music: %s", w.c_str());
                    delete t;
                    vTaskDelete(nullptr);
                }, "hg_music_wait", 4096, new std::string(track), 2, nullptr);
            });
            return std::string("{\"ok\":true,\"note\":\"starts when she finishes talking\"}");
        });

    ESP_LOGI(TAG, "agent tools registered (SD root %s)", kRoot);
}

}  // namespace hg_agent
