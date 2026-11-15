// headphone-girl v3.6.1: esp-dl face detector wrapper. See hg_face_dl.h.
#include "hg_face_dl.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <sdkconfig.h>

#include "human_face_detect.hpp"

#define TAG "hg_face_dl"

namespace hg_face_dl {
namespace {
HumanFaceDetect* g_det = nullptr;
bool g_failed = false;
int g_last_ms = 0;
int g_last_count = 0;
constexpr int kMaxBoxes = 8;
}  // namespace

// v3.6.3: load with every plain malloc/new sent to PSRAM.
// Root cause of v3.6.2's 16.8 KB: the model graph is hundreds of small C++
// objects (vectors, strings, map nodes, module/tensor shells). Plain malloc
// keeps anything <= CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (1024 B) in internal
// SRAM, so the patched dl::tool allocator never sees them. For the ~0.3 s of
// the load we lower that limit to 0 (global, then restored), so they land in
// PSRAM too. Nothing in the model is DMA or ISR data, so PSRAM is fine.
bool Load() {
    if (g_det != nullptr) return true;
    if (g_failed) return false;
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t before_ps = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    int64_t t0 = esp_timer_get_time();
    heap_caps_malloc_extmem_enable(0);
    g_det = new (std::nothrow) HumanFaceDetect(HumanFaceDetect::MSRMNP_S8_V1, false);
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
    if (g_det == nullptr) {
        g_failed = true;
        ESP_LOGE(TAG, "face model failed to load, follow faces off");
        return false;
    }
    ESP_LOGI(TAG, "face model loaded in %d ms (MSR+MNP s8), internal RAM used %d B, PSRAM used %d B "
                  "(other tasks may skew these)",
             (int)((esp_timer_get_time() - t0) / 1000),
             (int)before - (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (int)before_ps - (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
}

int LastMs() { return g_last_ms; }
int LastCount() { return g_last_count; }
bool Loaded() { return g_det != nullptr; }
bool Failed() { return g_failed; }

bool Release() {
    if (g_det == nullptr) return false;
    delete g_det;
    g_det = nullptr;
    return true;
}

bool Detect(const uint8_t* data, int w, int h, int kind, hg_gaze::Face* face) {
    *face = hg_gaze::Face{};
    if (g_failed || data == nullptr || kind < 0 || kind > 3) return false;
    if (g_det == nullptr && !Load()) return false;
    static const dl::image::pix_type_t kPix[4] = {
        dl::image::DL_IMAGE_PIX_TYPE_YUYV, dl::image::DL_IMAGE_PIX_TYPE_UYVY,
        dl::image::DL_IMAGE_PIX_TYPE_RGB565LE, dl::image::DL_IMAGE_PIX_TYPE_RGB565BE};
    dl::image::img_t img = {};
    img.data = const_cast<uint8_t*>(data);
    img.width = (uint16_t)w;
    img.height = (uint16_t)h;
    img.pix_type = kPix[kind];
    int64_t t0 = esp_timer_get_time();
    auto& res = g_det->run(img);
    g_last_ms = (int)((esp_timer_get_time() - t0) / 1000);
    hg_gaze::Box boxes[kMaxBoxes];
    int n = 0;
    for (const auto& r : res) {
        if (n >= kMaxBoxes) break;
        if (r.box.size() < 4) continue;
        boxes[n].x0 = r.box[0];
        boxes[n].y0 = r.box[1];
        boxes[n].x1 = r.box[2];
        boxes[n].y1 = r.box[3];
        boxes[n].score = r.score;
        ++n;
    }
    g_last_count = (int)res.size();
    *face = hg_gaze::PickFace(boxes, n, w, h);
    return face->found;
}

}  // namespace hg_face_dl
