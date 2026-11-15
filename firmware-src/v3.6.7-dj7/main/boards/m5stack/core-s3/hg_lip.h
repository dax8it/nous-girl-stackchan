// headphone-girl v3.5: background vowel analysis of the PCM she is playing.
#pragma once

namespace hg_lip {
// Start the low-priority analysis task (once, from board init).
void Start();
// Only analyse while the mouth overlay is moving (saves CPU during music).
void SetActive(bool on);
// NVS hg/vowel (default on). Off = the v3.4 loudness + zcr mouth.
bool VowelEnabled();
void SetVowelEnabled(bool on);
}  // namespace hg_lip
