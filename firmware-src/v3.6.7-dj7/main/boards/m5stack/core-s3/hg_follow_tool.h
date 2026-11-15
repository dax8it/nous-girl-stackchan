// headphone-girl v3.6.3: voice switch for "Follow faces".
// The MCP tool self.camera.follow_faces calls Apply(). It flips the SAME hook
// the Settings > Head > "Follow faces (camera)" switch uses (atomic + NVS
// fairy_gaze/follow + start the tracker), then redraws Settings if it is open.
// Pure code (std only) so the host unit test can include it.
#pragma once
#include <functional>
#include <string>

namespace hg_follow_tool {

constexpr const char* kToolName = "self.camera.follow_faces";

constexpr const char* kToolDescription =
    "Turn face following on or off. When on, she uses her camera to find a face and turns her "
    "head to keep looking at that person, by herself, until it is turned off. Same as Settings > "
    "Head > Follow faces, and it stays saved after a restart.\n"
    "enabled=true when the user says: follow me, track me, watch me, keep looking at me, "
    "look at me while I move.\n"
    "enabled=false when the user says: stop following, stop following me, stop tracking, "
    "stop looking at me, stop watching me.\n"
    "While following is on her head moves by itself: do NOT send self.servo.set_angles or other "
    "head moves unless the user asks for a specific head move. To look somewhere else, turn "
    "following off first.\n"
    "Returns JSON: follow_faces (the new state, true/false), camera_enabled, tracker_running "
    "and a short note to tell the user.";

struct Hooks {
    std::function<bool()> get_follow;          // current switch state
    std::function<void(bool)> set_follow;      // the Settings switch hook (atomic + NVS + start task)
    std::function<bool()> camera_enabled;      // Settings > Camera
    std::function<bool()> tracker_running;     // follow task exists
    std::function<void()> refresh_ui;          // redraw Settings / old menu if open (may be empty)
};

inline const char* B(bool v) { return v ? "true" : "false"; }

inline std::string StateJson(bool follow, bool camera, bool running) {
    return std::string("{\"follow_faces\":") + B(follow) + ",\"camera_enabled\":" + B(camera) +
           ",\"tracker_running\":" + B(running) + "}";
}

inline std::string NoteFor(bool follow, bool camera, bool running) {
    if (!follow) return "Stopped following faces. My head stays where it is.";
    if (!camera) return "Follow faces is on, but my camera is turned off in Settings > Camera, "
                        "so I cannot see faces until it is turned back on.";
    if (!running) return "Follow faces is on, but the tracker could not start (low memory). "
                         "It will start next time I restart.";
    return "Following faces now. My head will turn to keep looking at you.";
}

inline std::string Apply(const Hooks& h, bool enabled) {
    if (!h.get_follow || !h.set_follow) {
        return "{\"ok\":false,\"error\":\"follow faces is not available on this board\"}";
    }
    bool before = h.get_follow();
    // Always pass through the hook: an "on" request retries starting the tracker.
    h.set_follow(enabled);
    bool after = h.get_follow();
    if (h.refresh_ui) h.refresh_ui();
    bool cam = h.camera_enabled ? h.camera_enabled() : true;
    bool run = h.tracker_running ? h.tracker_running() : after;
    std::string out = "{\"ok\":";
    out += B(after == enabled);
    out += ",\"follow_faces\":";
    out += B(after);
    out += ",\"changed\":";
    out += B(before != after);
    out += ",\"camera_enabled\":";
    out += B(cam);
    out += ",\"tracker_running\":";
    out += B(run);
    out += ",\"note\":\"";
    out += NoteFor(after, cam, run);
    out += "\"}";
    return out;
}

}  // namespace hg_follow_tool
