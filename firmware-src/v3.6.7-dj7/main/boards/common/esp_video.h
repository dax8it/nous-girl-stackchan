#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "esp_video_init.h"
#include "jpg/image_to_jpeg.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class EspVideo : public Camera {
private:
    struct FrameBuffer {
        uint8_t* data = nullptr;
        size_t len = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        v4l2_pix_fmt_t format = 0;
    } frame_;
    v4l2_pix_fmt_t sensor_format_ = 0;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_ = 0;
    uint16_t sensor_height_ = 0;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    int video_fd_ = -1;
    bool streaming_on_ = false;
    struct MmapBuffer {
        void* start = nullptr;
        size_t length = 0;
    };
    std::vector<MmapBuffer> mmap_buffers_;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    std::mutex cam_mutex_;   // Capture() and GrabFrame() share the stream

public:
    EspVideo(const esp_video_init_config_t& config);
    ~EspVideo() override;

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::expected<std::string, std::string> Explain(const std::string& question) override;
    virtual bool CaptureJpeg(std::string& jpeg, int quality = 80) override;

    // headphone-girl v3.6 (follow faces): copy the newest raw frame (no screen preview). kind: 0 YUYV,
    // 1 UYVY, 2 RGB565 LE, 3 RGB565 BE. False if off, busy or unsupported.
    bool GrabFrame(uint8_t* dst, size_t cap, int* w, int* h, int* kind);
    // v3.6.2: helpers for photos
    bool ShowPreview();
    bool EncodeFrameJpeg(int quality, uint8_t** out, size_t* out_len);
};
