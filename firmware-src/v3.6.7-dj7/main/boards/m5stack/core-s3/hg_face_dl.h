// headphone-girl v3.6: real face detection for "Follow faces" with
// Espressif esp-dl human_face_detect (MSR+MNP int8, MIT). Model in flash rodata.
#pragma once
#include <cstdint>
#include "hg_gaze.h"

namespace hg_face_dl {
// kind: 0 YUYV, 1 UYVY, 2 RGB565 LE, 3 RGB565 BE (see EspVideo::GrabFrame).
// Fills *face with the biggest face (see hg_gaze::PickFace). False if none.
bool Detect(const uint8_t* data, int w, int h, int kind, hg_gaze::Face* face);
int LastMs();      // last detect time in ms (for the log)
bool Failed();     // model could not load: follow faces stays still
bool Release();    // free the detector (PSRAM); true if it was loaded
int LastCount();   // v3.6.2: boxes the model returned last time (before our filter)
bool Loaded();     // v3.6.2: model currently in memory
bool Load();       // v3.6.3: load now (small objects to PSRAM too); true if loaded
}  // namespace hg_face_dl
