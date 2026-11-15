// v3.6.7-dj6: pure helpers for self.screen.show_image (no ESP-IDF, no LVGL),
// so they can be unit-tested on the host (host-tests/test_meme.cc).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace hg_meme {

// Only pictures served by Alex's bridge. The prefix ends in "/", so a
// "user@host" or "host.evil.com" trick can't change the host.
constexpr const char* kUrlPrefix = "https://your-server.example.invalid/hg/";
constexpr size_t kMaxUrl = 300;
constexpr size_t kMaxBytes = 600 * 1024;       // same cap as the bridge
constexpr int kMaxSrcW = 640, kMaxSrcH = 480;  // same cap as the bridge
constexpr uint32_t kMaxSrcPixels = 320 * 240;  // decoded ARGB8888 must fit LVGL's 1 MB pool
constexpr int kScreenW = 320, kScreenH = 240;
constexpr int kDefaultSeconds = 8, kMinSeconds = 1, kMaxSeconds = 20;

inline bool UrlAllowed(const std::string& url) {
    const size_t n = std::strlen(kUrlPrefix);
    if (url.size() <= n || url.size() > kMaxUrl) return false;
    if (url.compare(0, n, kUrlPrefix) != 0) return false;
    for (unsigned char c : url) {
        if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '#') return false;
    }
    if (url.find("..") != std::string::npos) return false;
    if (url.find("%2e") != std::string::npos || url.find("%2E") != std::string::npos) return false;
    return true;
}

// Width/height from a PNG's IHDR. false if it isn't a PNG.
inline bool PngDims(const uint8_t* d, size_t n, uint32_t* w, uint32_t* h) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (d == nullptr || n < 33 || std::memcmp(d, sig, 8) != 0 || std::memcmp(d + 12, "IHDR", 4) != 0)
        return false;
    *w = (uint32_t(d[16]) << 24) | (uint32_t(d[17]) << 16) | (uint32_t(d[18]) << 8) | d[19];
    *h = (uint32_t(d[20]) << 24) | (uint32_t(d[21]) << 16) | (uint32_t(d[22]) << 8) | d[23];
    return *w > 0 && *h > 0;
}

// "" if the picture can be shown, else why not.
inline const char* CheckDims(uint32_t w, uint32_t h) {
    if (w > uint32_t(kMaxSrcW) || h > uint32_t(kMaxSrcH)) return "png larger than 640x480";
    if (uint64_t(w) * h > kMaxSrcPixels) return "png has more pixels than 320x240; send 320x240";
    return "";
}

// Largest size with the same shape that fits the screen (scales up or down).
inline void FitSize(int w, int h, int* ow, int* oh) {
    if (int64_t(w) * kScreenH >= int64_t(h) * kScreenW) {
        *ow = kScreenW;
        *oh = int((int64_t(h) * kScreenW + w / 2) / w);
    } else {
        *oh = kScreenH;
        *ow = int((int64_t(w) * kScreenH + h / 2) / h);
    }
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
    if (*ow > kScreenW) *ow = kScreenW;
    if (*oh > kScreenH) *oh = kScreenH;
}

inline int ClampSeconds(int s) {
    return s < kMinSeconds ? kMinSeconds : (s > kMaxSeconds ? kMaxSeconds : s);
}

inline uint16_t Rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

// LVGL ARGB8888 (bytes B,G,R,A per pixel, row stride in bytes) -> RGB565
// (ow x oh, nearest neighbour, alpha blended onto black).
inline void ArgbToRgb565(const uint8_t* src, int w, int h, int stride, uint16_t* dst, int ow, int oh) {
    for (int y = 0; y < oh; y++) {
        const int sy = int(int64_t(y) * h / oh);
        const uint8_t* row = src + size_t(sy) * stride;
        for (int x = 0; x < ow; x++) {
            const int sx = int(int64_t(x) * w / ow);
            const uint8_t* p = row + size_t(sx) * 4;
            const unsigned a = p[3];
            dst[size_t(y) * ow + x] = Rgb565(uint8_t(p[2] * a / 255), uint8_t(p[1] * a / 255),
                                             uint8_t(p[0] * a / 255));
        }
    }
}

}  // namespace hg_meme
