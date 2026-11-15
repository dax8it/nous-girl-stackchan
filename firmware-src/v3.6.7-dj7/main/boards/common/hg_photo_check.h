// headphone-girl v3.6.4: cheap checks on a photo before it is uploaded.
//
// 1. FrameStats(): luma mean / spread / min / max over a sparse grid of the raw
//    frame. A frame with (almost) no spread is not a picture of anything: the
//    vision model answers "you did not provide a picture" for a flat grey frame
//    (measured on gemma4:e4b, 2026-10-23). Capture() retries such a frame and
//    take_photo reports it instead of sending it.
// 2. JpegLooksValid(): SOI at the start and EOI at the end.
//
// Pure C++ (no ESP-IDF headers): the host test includes it directly.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hg_photo {

// kind: 0 YUYV, 1 UYVY, 2 RGB565 little endian, 3 RGB565 big endian, 4 GREY
struct Stats {
    int samples = 0;
    int mean = 0;        // 0..255
    float stddev = 0;    // luma standard deviation
    int min = 255;
    int max = 0;
};

inline int LumaAt(const uint8_t* p, size_t len, int w, int x, int y, int kind) {
    if (kind == 4) {
        size_t o = (size_t)y * w + x;
        return o < len ? p[o] : -1;
    }
    if (kind == 0 || kind == 1) {
        size_t o = ((size_t)y * w + (x & ~1)) * 2;
        if (o + 3 >= len) return -1;
        if (kind == 0) return p[o + ((x & 1) ? 2 : 0)];
        return p[o + ((x & 1) ? 3 : 1)];
    }
    size_t o = ((size_t)y * w + x) * 2;
    if (o + 1 >= len) return -1;
    uint16_t v = (kind == 2) ? (uint16_t)(p[o] | (p[o + 1] << 8)) : (uint16_t)((p[o] << 8) | p[o + 1]);
    int r = ((v >> 11) & 0x1f) << 3, g = ((v >> 5) & 0x3f) << 2, b = (v & 0x1f) << 3;
    return (77 * r + 150 * g + 29 * b) >> 8;
}

inline Stats FrameStats(const uint8_t* p, size_t len, int w, int h, int kind, int step = 4) {
    Stats s;
    if (p == nullptr || w <= 0 || h <= 0 || step <= 0 || kind < 0 || kind > 4) return s;
    double sum = 0, sum2 = 0;
    for (int y = step / 2; y < h; y += step) {
        for (int x = step / 2; x < w; x += step) {
            int l = LumaAt(p, len, w, x, y, kind);
            if (l < 0) continue;
            ++s.samples;
            sum += l;
            sum2 += (double)l * l;
            if (l < s.min) s.min = l;
            if (l > s.max) s.max = l;
        }
    }
    if (s.samples == 0) {
        s.min = 0;
        return s;
    }
    double m = sum / s.samples;
    double var = sum2 / s.samples - m * m;
    s.mean = (int)(m + 0.5);
    s.stddev = var > 0 ? (float)std::sqrt(var) : 0.f;
    return s;
}

// A real camera frame always has sensor noise; a frame whose luma spans at
// most 3 levels over ~4800 samples is a stuck, blank or unwritten buffer. Kept
// this strict so a plain wall or a dark room still counts as a picture.
inline bool IsFlat(const Stats& s) {
    if (s.samples < 16) return true;
    return (s.max - s.min) <= 3;
}

inline bool JpegLooksValid(const uint8_t* p, size_t len) {
    if (p == nullptr || len < 128) return false;
    if (p[0] != 0xFF || p[1] != 0xD8 || p[2] != 0xFF) return false;
    // EOI may be followed by a little padding
    size_t start = len > 16 ? len - 16 : 0;
    for (size_t i = len - 1; i > start; --i) {
        if (p[i - 1] == 0xFF && p[i] == 0xD9) return true;
    }
    return false;
}

}  // namespace hg_photo
