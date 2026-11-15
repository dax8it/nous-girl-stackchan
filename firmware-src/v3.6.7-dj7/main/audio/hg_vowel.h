// hg_vowel.h: Japanese/English vowel estimate (a, i, u, e, o) for lip sync.
//
// Ported from m5unified-avatar-mic (commit 8b6f79f), files VowelModel.h,
// LpcFormants.h and VowelAnalyzer.h:
//   https://github.com/mongonta0716/m5unified-avatar-mic
//
// MIT License
// Copyright (c) 2026 Takao Akaki
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// headphone-girl v3.5 changes (also MIT): input is her own 24 kHz playback PCM
// instead of a 16 kHz stereo mic, so the front end is a 31-tap low-pass at
// 3.3 kHz with decimation by 3 (768 samples = 32 ms -> 256 samples at 8 kHz).
// The vowel centers are calibrated for the Phonon "Harper" voice. The LPC,
// root solver, voicing gate, classifier and stabilizer are unchanged.
// No device headers here, so the same file builds in host unit tests.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace hg_vowel {

enum class Vowel : uint8_t { Unknown, A, I, U, E, O };

inline const char* VowelName(Vowel v) {
    static const char* names[] = {"-", "a", "i", "u", "e", "o"};
    const unsigned index = static_cast<unsigned>(v);
    return names[index < 6 ? index : 0];
}

struct VowelModel {
    // index 0..4 = a, i, u, e, o
    float f1[5] = {800, 300, 350, 500, 500};
    float f2[5] = {1200, 2300, 1300, 1900, 850};
    float noiseRms = 35;
};

// Harper (Phonon 31M) centers, fitted by scripts/hg_v35/calibrate.py from
// sustained "ahh, eee, ooo, ehh, ohh" clips. Rerun that script if her voice changes.
#include "hg_vowel_model.inc"

struct VowelFeatures {
    uint32_t sequence = 0;
    float f1 = 0, f2 = 0;
    float rms = 0, voicing = 0, confidence = 0;
    float rawConfidence = 0, distance = 0;
    Vowel vowel = Vowel::Unknown;
};

inline Vowel ClassifyVowel(float f1, float f2, const VowelModel& model, float& confidence,
                           float* rawConfidence = nullptr, float* distanceOut = nullptr) {
    confidence = 0;
    if (rawConfidence) *rawConfidence = 0;
    if (distanceOut) *distanceOut = 0;
    if (!(f1 >= 180 && f1 <= 1200 && f2 >= 500 && f2 <= 3300 && f2 - f1 >= 150))
        return Vowel::Unknown;
    float best = 1e9f, second = 1e9f;
    unsigned nearest = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const float x = std::log(f1 / model.f1[i]) / 0.35f;
        const float y = std::log(f2 / model.f2[i]) / 0.25f;
        const float distance = x * x + y * y;
        if (distance < best) { second = best; best = distance; nearest = i; }
        else second = std::min(second, distance);
    }
    confidence = std::exp(-0.5f * best) * (second - best) / (second + 0.1f);
    if (rawConfidence) *rawConfidence = confidence;
    if (distanceOut) *distanceOut = best;
    if (best > 4 || confidence < 0.25f) { confidence = 0; return Vowel::Unknown; }
    return static_cast<Vowel>(nearest + 1);
}

// Formants from LPC polynomial roots (bounded Durand-Kerner). Unchanged.
class LpcFormants {
public:
    static constexpr size_t kOrder = 10;
    struct Pair { float f1 = 0, f2 = 0; };

    Pair Extract(const std::array<float, kOrder + 1>& coefficients) {
        constexpr float pi = 3.14159265358979323846f;
        for (size_t i = 0; i < kOrder; ++i) {
            const float angle = 2 * pi * i / kOrder + 0.17f;
            roots_[i] = std::polar(0.95f, angle);
        }
        for (unsigned iteration = 0; iteration < 80; ++iteration) {
            float largest = 0;
            for (size_t i = 0; i < kOrder; ++i) {
                const auto z = roots_[i];
                auto value = std::complex<float>(coefficients[0], 0);
                for (size_t j = 1; j <= kOrder; ++j) value = value * z + coefficients[j];
                auto denominator = std::complex<float>(1, 0);
                for (size_t j = 0; j < kOrder; ++j) if (i != j) denominator *= z - roots_[j];
                if (std::norm(denominator) < 1e-20f) return Pair{};
                auto delta = value / denominator;
                const float step = std::abs(delta);
                if (!std::isfinite(step)) return Pair{};
                if (step > 0.5f) delta *= 0.5f / step;
                roots_[i] -= delta;
                largest = std::max(largest, std::abs(delta));
            }
            if (largest < 0.0001f) break;
        }
        float coefficientSum = 0;
        for (const auto coefficient : coefficients) coefficientSum += std::fabs(coefficient);
        size_t count = 0;
        for (const auto z : roots_) {
            if (z.imag() <= 0.01f) continue;
            auto residual = std::complex<float>(coefficients[0], 0);
            for (size_t j = 1; j <= kOrder; ++j) residual = residual * z + coefficients[j];
            if (std::abs(residual) > coefficientSum * 0.0001f) return Pair{};
            float radius = std::abs(z);
            if (radius > 1) radius = 1 / radius;
            const float frequency = std::arg(z) * 8000 / (2 * pi);
            const float bandwidth = -std::log(radius) * 8000 / pi;
            if (frequency < 180 || frequency > 3300 || bandwidth < 20 || bandwidth > 400) continue;
            frequencies_[count++] = frequency;
        }
        std::sort(frequencies_.begin(), frequencies_.begin() + count);
        Pair result;
        for (size_t i = 0; i < count; ++i) {
            const float frequency = frequencies_[i];
            if (result.f1 == 0 && frequency <= 1200) result.f1 = frequency;
            else if (result.f1 > 0 && frequency >= 500 && frequency - result.f1 >= 150) {
                result.f2 = frequency; break;
            }
        }
        return result;
    }

private:
    std::array<std::complex<float>, kOrder> roots_{};
    std::array<float, kOrder> frequencies_{};
};

// 24 kHz mono front end. Push() keeps the last 768 samples; Analyze() looks at
// them (32 ms) and updates result(). Call Analyze() once per 20 ms hop.
class Analyzer24k {
public:
    static constexpr size_t kInput = 768;
    static constexpr size_t kSamples = 256;
    static constexpr size_t kOrder = 10;

    Analyzer24k() {
        for (size_t i = 0; i < kSamples; ++i)
            window_[i] = 0.54f - 0.46f * std::cos(2 * kPi * i / (kSamples - 1));
        SetModel(kHarperModel);
    }

    void SetModel(const VowelModel& model) { model_ = model; Reset(); }
    void Reset() {
        history_.fill(0);
        head_ = 0;
        result_ = VowelFeatures{};
        candidate_ = held_ = Vowel::Unknown;
        candidateFrames_ = weakFrames_ = 0;
        heldConfidence_ = 0;
    }

    void Push(const int16_t* pcm, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            history_[head_] = pcm[i];
            head_ = (head_ + 1) % kInput;
        }
    }

    const VowelFeatures& Analyze() {
        ++result_.sequence;
        result_.f1 = result_.f2 = result_.voicing = 0;
        result_.rawConfidence = result_.distance = 0;
        static constexpr int kTaps = 31;
        static constexpr float fir[kTaps] = {
            0.0f, -0.0000414f, -0.0003989f, -0.0008627f, -0.0001749f, 0.0029251f,
            0.0070767f, 0.0067681f, -0.0041813f, -0.0240897f, -0.0370423f, -0.0183439f,
            0.0470653f, 0.1461930f, 0.2376505f, 0.2749126f, 0.2376505f, 0.1461930f,
            0.0470653f, -0.0183439f, -0.0370423f, -0.0240897f, -0.0041813f, 0.0067681f,
            0.0070767f, 0.0029251f, -0.0001749f, -0.0008627f, -0.0003989f, -0.0000414f, 0.0f};
        // unroll the ring into time order once
        for (size_t i = 0; i < kInput; ++i) input_[i] = (float)history_[(head_ + i) % kInput];
        float mean = 0;
        for (size_t i = 0; i < kSamples; ++i) {
            float sample = 0;
            const int c = (int)(i * 3) + 1;
            for (int tap = 0; tap < kTaps; ++tap) {
                const int index = std::max(0, std::min((int)kInput - 1, c + tap - kTaps / 2));
                sample += fir[tap] * input_[index];
            }
            mono_[i] = sample;
            mean += sample;
        }
        mean /= kSamples;
        float power = 0;
        for (auto& sample : mono_) { sample -= mean; power += sample * sample; }
        result_.rms = std::sqrt(power / kSamples);
        if (result_.rms < 20.0f) { Stabilize(Vowel::Unknown, 0); return result_; }
        float voicing = 0;
        for (size_t lag = 20; lag <= 100; ++lag) {   // 80..400 Hz fundamental
            float xy = 0, xx = 0, yy = 0;
            for (size_t i = lag; i < kSamples; ++i) {
                const float x = mono_[i], y = mono_[i - lag];
                xy += x * y; xx += x * x; yy += y * y;
            }
            voicing = std::max(voicing, xy / std::sqrt(xx * yy + 1e-12f));
        }
        result_.voicing = voicing;
        if (voicing < 0.55f) { Stabilize(Vowel::Unknown, 0); return result_; }
        float previous = 0;
        for (size_t i = 0; i < kSamples; ++i) {
            work_[i] = (mono_[i] - 0.97f * previous) * window_[i];
            previous = mono_[i];
        }
        for (size_t lag = 0; lag <= kOrder; ++lag) {
            float sum = 0;
            for (size_t i = lag; i < kSamples; ++i) sum += work_[i] * work_[i - lag];
            ac_[lag] = sum;
        }
        if (ac_[0] < 1e-6f) { Stabilize(Vowel::Unknown, 0); return result_; }
        for (size_t i = 1; i <= kOrder; ++i) ac_[i] /= ac_[0];
        ac_[0] = 1.0001f;
        lpc_.fill(0); lpc_[0] = 1;
        float error = ac_[0];
        for (size_t order = 1; order <= kOrder; ++order) {
            float reflection = ac_[order];
            for (size_t i = 1; i < order; ++i) reflection += lpc_[i] * ac_[order - i];
            reflection = -reflection / error;
            if (!std::isfinite(reflection) || std::fabs(reflection) >= 0.9999f) {
                Stabilize(Vowel::Unknown, 0); return result_;
            }
            previous_ = lpc_;
            for (size_t i = 1; i < order; ++i) lpc_[i] = previous_[i] + reflection * previous_[order - i];
            lpc_[order] = reflection;
            error *= 1 - reflection * reflection;
            if (error < 1e-7f) { Stabilize(Vowel::Unknown, 0); return result_; }
        }
        const auto pair = formants_.Extract(lpc_);
        result_.f1 = pair.f1; result_.f2 = pair.f2;
        if (result_.rms < model_.noiseRms * 1.8f) { Stabilize(Vowel::Unknown, 0); return result_; }
        float confidence = 0;
        const auto vowel = ClassifyVowel(pair.f1, pair.f2, model_, confidence,
                                         &result_.rawConfidence, &result_.distance);
        Stabilize(vowel, confidence);
        return result_;
    }

    const VowelFeatures& result() const { return result_; }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    std::array<int16_t, kInput> history_{};
    size_t head_ = 0;
    std::array<float, kInput> input_{};
    std::array<float, kSamples> mono_{}, work_{}, window_{};
    std::array<float, kOrder + 1> ac_{}, lpc_{}, previous_{};
    LpcFormants formants_;
    VowelModel model_;
    VowelFeatures result_;
    Vowel candidate_ = Vowel::Unknown, held_ = Vowel::Unknown;
    unsigned candidateFrames_ = 0, weakFrames_ = 0;
    float heldConfidence_ = 0;

    void Stabilize(Vowel vowel, float confidence) {
        if (vowel == Vowel::Unknown) {
            candidate_ = Vowel::Unknown;
            candidateFrames_ = 0;
            if (++weakFrames_ >= 3) { held_ = Vowel::Unknown; heldConfidence_ = 0; }
            else heldConfidence_ *= 0.5f;
        } else {
            weakFrames_ = 0;
            if (candidate_ != vowel) { candidate_ = vowel; candidateFrames_ = 1; }
            else ++candidateFrames_;
            if (candidateFrames_ >= 2) { held_ = vowel; heldConfidence_ = confidence; }
            else heldConfidence_ *= 0.7f;
        }
        result_.vowel = held_;
        result_.confidence = heldConfidence_;
    }
};

}  // namespace hg_vowel
