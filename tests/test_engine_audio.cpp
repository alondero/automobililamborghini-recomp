// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
using namespace lambo::engine_audio;
static void require(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
static double energy(const std::vector<int16_t>& pcm, int channel = 0) {
    double result = 0;
    for (size_t i = channel; i < pcm.size(); i += 2) result += double(pcm[i]) * pcm[i];
    return result / (pcm.size() / 2);
}
static double tone(const std::vector<int16_t>& pcm, double hz, unsigned rate) {
    double re = 0, im = 0;
    for (size_t i = pcm.size() / 4; i < pcm.size() / 2; ++i) {
        const double a = 6.283185307179586 * hz * i / rate;
        re += pcm[i * 2] * std::cos(a); im += pcm[i * 2] * std::sin(a);
    }
    return re * re + im * im;
}
static void write_wav(const char* path) {
    constexpr unsigned rate = 48000, seconds = 14;
    std::vector<int16_t> pcm(rate * seconds * 2);
    Synth synth; Controls c;
    // Idle, three acceleration sweeps with shift drops, lift-off, then stop.
    for (unsigned frame = 0; frame < rate * seconds; frame += 480) {
        float t = float(frame) / rate;
        float pitch = t < 2 ? 0.1f : t < 11 ? 0.35f + std::fmod(t - 2, 3.0f) * 0.42f
                                            : std::max(0.1f, 1.3f - (t - 11) * 0.6f);
        c[0] = {pitch, t < 13 ? 0.55f : 0.0f, 0.5f};
        synth.mix(pcm.data() + frame * 2, 480, rate, c);
    }
    std::ofstream out(path, std::ios::binary);
    auto le = [&](uint32_t n, int bytes) { while (bytes--) { out.put(char(n & 255)); n >>= 8; } };
    out.write("RIFF", 4); le(36 + pcm.size() * 2, 4); out.write("WAVEfmt ", 8);
    le(16, 4); le(1, 2); le(2, 2); le(rate, 4); le(rate * 4, 4); le(4, 2); le(16, 2);
    out.write("data", 4); le(pcm.size() * 2, 4);
    for (auto s : pcm) le(uint16_t(s), 2);
    require(bool(out), "write audition WAV");
}
int main(int argc, char** argv) {
    if (argc == 2) { write_wav(argv[1]); return 0; }
    for (unsigned rate : {22050u, 32000u, 48000u}) {
        Controls c; Synth silent;
        std::vector<int16_t> pcm(rate * 2, 1234), original = pcm;
        silent.mix(pcm.data(), rate, rate, c);
        require(pcm == original, "inactive engines must preserve all original PCM");
        c[0] = {0.5f, 0.55f, 0.0f};
        Synth a, b; std::vector<int16_t> whole(rate * 2), chunks(rate * 2);
        a.mix(whole.data(), rate, rate, c);
        for (size_t i = 0; i < rate;) {
            size_t n = std::min(size_t(137), size_t(rate) - i);
            b.mix(chunks.data() + 2 * i, n, rate, c); i += n;
        }
        require(whole == chunks, "buffer boundaries must not reset oscillator/filter history");
        require(energy(whole) > 10000, "engine must be audible");
        // Ignore initial pan slew when comparing the hard-left voice.
        require(energy(whole, 1) < energy(whole) * 0.02, "guest pan must control stereo placement");
        const double fundamental = (410 + 4900 * 0.5) / 10;
        require(tone(whole, fundamental, rate) > tone(whole, fundamental * 0.8, rate) * 20,
                "combustion frequency must follow RPM");
        c[0].pitch = 1.0f;
        std::fill(whole.begin(), whole.end(), 0); a.mix(whole.data(), rate, rate, c);
        require(tone(whole, 531, rate) > tone(whole, 286, rate) * 20, "revs must raise engine frequency");
        c[0].gain = 0;
        std::fill(whole.begin(), whole.end(), 0); a.mix(whole.data(), rate, rate, c);
        require(std::all_of(whole.end() - 1000, whole.end(), [](auto s) { return s == 0; }),
                "stopped engines must fade to silence");
        c[0] = {std::numeric_limits<float>::quiet_NaN(), 1, 0.5f};
        a.mix(whole.data(), rate, rate, c); // Invalid control must remain bounded.
    }
    std::cout << "engine audio tests passed\n";
}
