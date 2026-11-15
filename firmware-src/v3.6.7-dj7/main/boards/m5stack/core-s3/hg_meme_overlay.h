// v3.6.7-dj6: full-screen meme overlay (self.screen.show_image).
#pragma once

class Display;

namespace hg_meme {
// Registers self.screen.show_image (a listed tool). Call once after the display exists.
void RegisterTool(Display* display);
bool IsShown();
// Hide the picture now (tap, new voice turn). Safe from any task; no-op if nothing is shown.
void Dismiss(const char* why);
}  // namespace hg_meme
