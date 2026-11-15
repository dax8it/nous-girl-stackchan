#ifndef CAMERA_H
#define CAMERA_H

#include <expected>
#include <string>
#include <atomic>

// headphone-girl v3: Settings > Camera on/off. Off = every capture fails.
inline std::atomic<bool>& HgCameraEnabled() {
    static std::atomic<bool> enabled{true};
    return enabled;
}

class Camera {
public:
    virtual ~Camera() = default;

    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::expected<std::string, std::string> Explain(const std::string& question) = 0;
    // headphone-girl patch: capture one frame and return it as JPEG bytes
    virtual bool CaptureJpeg(std::string& jpeg, int quality = 80) { return false; }
};

#endif  // CAMERA_H
