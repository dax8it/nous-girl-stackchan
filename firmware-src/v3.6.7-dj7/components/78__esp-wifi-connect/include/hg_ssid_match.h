// headphone-girl v3.6.5: forgiving SSID match for networks typed by hand.
//
// An iPhone hotspot cannot be picked from the robot's scan list (the hotspot
// switches off while the phone is joined to the robot's setup network), so its
// name is typed on the phone. iOS autocapitalises, may add a trailing space, and
// types curly apostrophes. Saved "cherib central " must still join the real
// "Cherib central". Exact match always wins; this is only the fallback.
#pragma once

#include <string>

namespace hg_ssid {

inline std::string Normalize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        // UTF-8 curly quotes: E2 80 98/99 (single), E2 80 9C/9D (double)
        if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80) {
            unsigned char d = (unsigned char)in[i + 2];
            if (d == 0x98 || d == 0x99) { out.push_back('\''); i += 2; continue; }
            if (d == 0x9C || d == 0x9D) { out.push_back('"'); i += 2; continue; }
        }
        // UTF-8 no-break space C2 A0 -> space
        if (c == 0xC2 && i + 1 < in.size() && (unsigned char)in[i + 1] == 0xA0) {
            out.push_back(' ');
            i += 1;
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        out.push_back((char)c);
    }
    // trim ASCII whitespace both ends
    size_t b = 0, e = out.size();
    while (b < e && (out[b] == ' ' || out[b] == '\t' || out[b] == '\r' || out[b] == '\n')) ++b;
    while (e > b && (out[e - 1] == ' ' || out[e - 1] == '\t' || out[e - 1] == '\r' || out[e - 1] == '\n')) --e;
    return out.substr(b, e - b);
}

inline bool Exact(const std::string& seen, const std::string& saved) { return seen == saved; }

inline bool Loose(const std::string& seen, const std::string& saved) {
    if (seen.empty() || saved.empty()) return false;
    return Normalize(seen) == Normalize(saved);
}

// WPA2/WPA3 personal: empty (open network) or 8..63 characters (64 = hex PSK).
inline bool PasswordLooksValid(const std::string& pw) {
    return pw.empty() || (pw.size() >= 8 && pw.size() <= 64);
}

}  // namespace hg_ssid
