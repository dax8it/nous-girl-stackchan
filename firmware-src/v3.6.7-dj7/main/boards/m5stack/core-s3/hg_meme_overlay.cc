// v3.6.7-dj6: self.screen.show_image(url, seconds) draws a PNG full screen
// (320x240) on LVGL's top layer, above the Fairy face, the classic geometry
// face (geometry_root_) and the DJ screen, then removes it after `seconds`.
//
// Unlike the stock self.screen.preview_image (user-only, scale 0.5, so a
// 320x240 meme shows at 160x120 for 5 s) this tool is LISTED, so the server
// can see it, and it is limited to https://your-server.example.invalid/hg/ URLs.
//
// The PNG is decoded ONCE (LVGL lodepng, ARGB8888, under the display lock)
// and converted to RGB565 in PSRAM (153.6 KB). LVGL's image cache is 0 on
// this build, so drawing the PNG itself would re-decode it on every redraw.
// A tap on the screen or the next voice turn (user chat message) hides it early.
#include "hg_meme_overlay.h"
#include "hg_meme_logic.h"
#include "hg_settings.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <lvgl_private.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

#include "board.h"
#include "display/display.h"
#include "mcp_server.h"

#define TAG "HgMeme"

namespace hg_meme {
namespace {

Display* g_display = nullptr;
lv_obj_t* g_root = nullptr;        // full-screen black box on lv_layer_top()
uint16_t* g_pixels = nullptr;      // RGB565, PSRAM
lv_image_dsc_t g_dsc;
esp_timer_handle_t g_timer = nullptr;
std::atomic<bool> g_shown{false};
std::atomic<bool> g_busy{false};

struct Busy {
    bool ok;
    Busy() : ok(!g_busy.exchange(true)) {}
    ~Busy() { if (ok) g_busy.store(false); }
};

using Buf = std::unique_ptr<uint8_t, decltype(&heap_caps_free)>;

// Caller holds the display lock.
void RemoveLocked() {
    if (g_timer) esp_timer_stop(g_timer);
    if (g_root) {
        lv_obj_delete(g_root);   // also deletes the image child; its area is redrawn (face comes back)
        g_root = nullptr;
    }
    if (g_pixels) {
        heap_caps_free(g_pixels);
        g_pixels = nullptr;
    }
    g_shown.store(false);
}

void TimerCb(void*) { Dismiss("timer"); }

// Download into PSRAM. Works with Content-Length and with chunked / unknown length.
std::string Download(const std::string& url, Buf* out, size_t* out_len) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    if (auto opened = http->Open("GET", url); !opened) {
        return "could not open the picture URL (" + opened.error().ToString() + ")";
    }
    auto status = http->GetStatusCode();
    if (!status) { http->Close(); return status.error().ToString(); }
    if (*status != 200) { http->Close(); return "picture URL answered " + std::to_string(*status); }

    const size_t declared = http->GetBodyLength();
    if (declared > kMaxBytes) { http->Close(); return "png too big (over 600 KB)"; }
    const size_t cap = declared > 0 ? declared : kMaxBytes + 1;
    Buf data(static_cast<uint8_t*>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             heap_caps_free);
    if (!data) { http->Close(); return "out of memory for the picture"; }
    size_t total = 0;
    while (total < cap) {
        auto ret = http->Read(reinterpret_cast<char*>(data.get()) + total, cap - total);
        if (!ret) { http->Close(); return "download failed"; }
        if (*ret <= 0) break;
        total += size_t(*ret);
    }
    http->Close();
    if (total > kMaxBytes) return "png too big (over 600 KB)";
    if (declared > 0 && total != declared) return "download cut short";
    if (total == 0) return "empty picture";
    *out = std::move(data);
    *out_len = total;
    return "";
}

// Decode + convert + show. Caller holds the display lock.
std::string ShowLocked(const uint8_t* png, size_t len, int seconds, int* ow_out, int* oh_out) {
    lv_image_dsc_t src;
    lv_memzero(&src, sizeof(src));
    src.data = png;
    src.data_size = len;

    lv_image_decoder_dsc_t dec;
    lv_memzero(&dec, sizeof(dec));
    if (lv_image_decoder_open(&dec, &src, nullptr) != LV_RESULT_OK || dec.decoded == nullptr) {
        return "the robot could not decode that PNG";
    }
    const lv_draw_buf_t* d = dec.decoded;
    const int w = d->header.w, h = d->header.h, stride = d->header.stride;
    if (d->header.cf != LV_COLOR_FORMAT_ARGB8888 || w <= 0 || h <= 0 || stride < w * 4) {
        lv_image_decoder_close(&dec);
        return "unexpected decoded format";
    }
    int ow = 0, oh = 0;
    FitSize(w, h, &ow, &oh);
    auto* px = static_cast<uint16_t*>(heap_caps_malloc(size_t(ow) * oh * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (px == nullptr) {
        lv_image_decoder_close(&dec);
        return "out of memory for the picture";
    }
    ArgbToRgb565(d->data, w, h, stride, px, ow, oh);
    lv_image_decoder_close(&dec);   // frees the ARGB8888 buffer (image cache is off)

    RemoveLocked();                 // replace a picture that is still up
    g_pixels = px;
    lv_memzero(&g_dsc, sizeof(g_dsc));
    g_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_dsc.header.w = ow;
    g_dsc.header.h = oh;
    g_dsc.header.stride = ow * 2;
    g_dsc.data = reinterpret_cast<const uint8_t*>(g_pixels);
    g_dsc.data_size = size_t(ow) * oh * 2;

    g_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(g_root, 0, 0);
    lv_obj_set_style_bg_color(g_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_CLICKABLE);   // taps are handled by the board (PollTouchpad)
    lv_obj_t* img = lv_image_create(g_root);
    lv_image_set_src(img, &g_dsc);
    lv_obj_center(img);
    lv_obj_move_foreground(g_root);
    g_shown.store(true);

    esp_timer_stop(g_timer);
    esp_timer_start_once(g_timer, uint64_t(seconds) * 1000000ULL);
    *ow_out = ow;
    *oh_out = oh;
    return "";
}

}  // namespace

bool IsShown() { return g_shown.load(); }

void Dismiss(const char* why) {
    if (!g_shown.load() || g_display == nullptr) return;
    DisplayLockGuard lock(g_display);
    if (!g_shown.load()) return;
    RemoveLocked();
    ESP_LOGI(TAG, "meme hidden (%s)", why ? why : "");
}

void RegisterTool(Display* display) {
    g_display = display;
    if (g_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = TimerCb;
        args.arg = nullptr;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "hg_meme";
        args.skip_unhandled_events = true;
        esp_timer_create(&args, &g_timer);
    }
    McpServer::GetInstance().AddTool(
        "self.screen.show_image",
        "Show a PNG picture (a saved meme) full screen, 320x240, above the face, then the face comes "
        "back. url must start with https://your-server.example.invalid/hg/ (the bridge gives it). "
        "seconds 1..20 (default 8). A tap or the next voice turn hides it early.",
        PropertyList({Property("url", kPropertyTypeString),
                      Property("seconds", kPropertyTypeInteger, kDefaultSeconds, kMinSeconds, kMaxSeconds)}),
        [](const PropertyList& p) -> ToolResult {
            const std::string url = p["url"].value<std::string>();
            const int seconds = ClampSeconds(p["seconds"].value<int>());
            if (!UrlAllowed(url)) return std::unexpected("url not allowed (only https://your-server.example.invalid/hg/...)");
            if (g_display == nullptr) return std::unexpected("no display");
            if (hg_settings::IsOpen()) return std::unexpected("the settings screen is open");
            Busy busy;
            if (!busy.ok) return std::unexpected("busy showing another picture");

            Buf data(nullptr, heap_caps_free);
            size_t len = 0;
            std::string err = Download(url, &data, &len);
            if (!err.empty()) return std::unexpected(err);
            uint32_t w = 0, h = 0;
            if (!PngDims(data.get(), len, &w, &h)) return std::unexpected("not a PNG (the robot decodes PNG only)");
            const char* why = CheckDims(w, h);
            if (*why) return std::unexpected(std::string(why));

            int ow = 0, oh = 0;
            {
                DisplayLockGuard lock(g_display);
                err = ShowLocked(data.get(), len, seconds, &ow, &oh);
            }
            if (!err.empty()) return std::unexpected(err);
            ESP_LOGI(TAG, "meme shown %ux%u -> %dx%d, %u bytes, %d s", (unsigned)w, (unsigned)h, ow, oh,
                     (unsigned)len, seconds);
            char b[160];
            snprintf(b, sizeof(b),
                     "{\"ok\":true,\"width\":%d,\"height\":%d,\"source\":\"%ux%u\",\"bytes\":%u,\"seconds\":%d}",
                     ow, oh, (unsigned)w, (unsigned)h, (unsigned)len, seconds);
            return std::string(b);
        });
}

}  // namespace hg_meme
