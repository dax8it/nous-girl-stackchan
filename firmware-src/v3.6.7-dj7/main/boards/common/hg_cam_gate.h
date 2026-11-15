// headphone-girl v3.6.2: who may use the camera right now.
//
// Photos (self.camera.take_photo, the face ID "__hg_faceid__ identify" photo,
// self.sd.save_photo) always win over Follow faces:
//   1. the photo side raises "photo active" and waits until the follow task
//      is out of its grab + detect step (it cannot start a new one while a
//      photo is active),
//   2. the photo side frees the face detector (PSRAM + any internal RAM it
//      holds) so the capture copy, preview and JPEG encode have room,
//   3. capture, encode, upload,
//   4. "photo active" drops, follow carries on (it reloads the model).
//
// Lock order: gate (photo flag) -> EspVideo::cam_mutex_. The follow task
// takes cam_mutex_ only inside TryEnterFollow()/LeaveFollow(), so it can never
// hold the mutex while a photo waits for the gate. Dekker style: each side
// first publishes its own flag, then reads the other's (seq_cst atomics), so
// at least one of them always sees the other.
//
// Pure C++ (no ESP-IDF headers): the host test includes it directly.
#pragma once
#include <atomic>
#include <chrono>
#include <thread>

namespace hg_cam_gate {

class Gate {
public:
    // follow side ----------------------------------------------------------
    // True: you may grab + detect now; you MUST call LeaveFollow() after.
    bool TryEnterFollow() {
        busy_.fetch_add(1);
        if (photo_.load() > 0) {
            busy_.fetch_sub(1);
            return false;
        }
        return true;
    }
    void LeaveFollow() { busy_.fetch_sub(1); }

    // photo side -----------------------------------------------------------
    // Raises the photo flag (nested / parallel photos are counted) and waits
    // up to wait_ms for the follow task to leave its step. Returns true when
    // the follow task is idle. Always pair with EndPhoto(), even on false.
    bool BeginPhoto(int wait_ms) {
        photo_.fetch_add(1);
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
        while (busy_.load() > 0) {
            if (std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }
    void EndPhoto() { photo_.fetch_sub(1); }

    bool PhotoActive() const { return photo_.load() > 0; }
    int FollowBusy() const { return busy_.load(); }

private:
    std::atomic<int> photo_{0};
    std::atomic<int> busy_{0};
};

inline Gate& Get() {
    static Gate g;
    return g;
}

// The board registers how to free the face detector (core-s3: hg_face_dl).
// Only called by the photo side after BeginPhoto() returned true, so the
// follow task is guaranteed not to be inside Detect().
using FreeHook = bool (*)();
inline std::atomic<FreeHook>& FreeHookSlot() {
    static std::atomic<FreeHook> h{nullptr};
    return h;
}
inline void SetFreeHook(FreeHook fn) { FreeHookSlot().store(fn); }
inline bool FreeDetector() {
    FreeHook fn = FreeHookSlot().load();
    return fn != nullptr && fn();
}

// RAII for the photo side.
class PhotoScope {
public:
    explicit PhotoScope(int wait_ms = 2500) {
        idle_ = Get().BeginPhoto(wait_ms);
        if (idle_) freed_ = FreeDetector();
    }
    ~PhotoScope() { Get().EndPhoto(); }
    bool follow_idle() const { return idle_; }
    bool freed_detector() const { return freed_; }
    PhotoScope(const PhotoScope&) = delete;
    PhotoScope& operator=(const PhotoScope&) = delete;

private:
    bool idle_ = false;
    bool freed_ = false;
};

}  // namespace hg_cam_gate
