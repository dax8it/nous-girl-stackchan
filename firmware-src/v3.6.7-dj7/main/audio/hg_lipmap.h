// headphone-girl v3.5: pick a mouth frame from loudness + vowel.
// Pure function, no device headers, so the host unit test can include it.
// Frames: 0 closed, 1 half, 2 open (A), 3 wide (E), 4 round (O),
//         5 small (U), 6 wide teeth (I), 7 closed smile.
#pragma once
#include <cstdint>

namespace hg_lipmap {

// vowel: 0 unknown, 1 a, 2 i, 3 u, 4 e, 5 o (hg_vowel::Vowel order).
// conf: 0..255. level: loudness 0..1 relative to the recent peak.
// smile_rest: show the closed smile instead of the flat closed mouth
// between words (happy moods).
inline int PickMouth(bool live, float level, uint8_t zcr, uint8_t vowel,
                     uint8_t conf, bool smile_rest) {
    const int rest = smile_rest ? 7 : 0;
    if (!live || level < 0.14f) return rest;
    if (vowel != 0 && conf >= 32) {
        switch (vowel) {
            case 1: return level < 0.38f ? 1 : 2;   // a
            case 2: return level < 0.25f ? 1 : 6;   // i
            case 3: return 5;                       // u
            case 4: return level < 0.38f ? 1 : 3;   // e
            case 5: return level < 0.38f ? 5 : 4;   // o
            default: break;
        }
    }
    // No confident vowel (consonants, breath, glides): the v3.4 rule.
    if (zcr > 140) return 6;                        // hiss: s, sh, f -> teeth
    if (level < 0.38f) return (zcr > 70) ? 1 : 5;
    if (level < 0.70f) return (zcr > 90) ? 3 : (zcr < 35 ? 4 : 2);
    return (zcr > 110) ? 3 : 2;
}

}  // namespace hg_lipmap
