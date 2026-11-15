// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace lambo::engine_audio;
static void require(bool ok, const std::string& why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
using Pcm = std::vector<int16_t>;
static double energy(const Pcm& pcm, int channel = 0, size_t from = 0) {
    double result = 0;
    size_t n = 0;
    for (size_t i = from * 2 + channel; i < pcm.size(); i += 2, ++n) result += double(pcm[i]) * pcm[i];
    return n ? result / n : 0;
}
// Power spectrum of the left channel's second half (after control slews settle).
static std::vector<double> spectrum(const Pcm& pcm, unsigned rate, double& bin_hz) {
    size_t n = 1;
    while (n * 2 <= pcm.size() / 4) n *= 2;
    std::vector<std::complex<double>> x(n);
    const size_t first = pcm.size() / 2 - n;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(6.283185307179586 * i / (n - 1));
        x[i] = pcm[(first + i) * 2] * w;
    }
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const auto step = std::polar(1.0, -6.283185307179586 / len);
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w = 1;
            for (size_t k = 0; k < len / 2; ++k, w *= step) {
                const auto u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v; x[i + k + len / 2] = u - v;
            }
        }
    }
    bin_hz = double(rate) / n;
    std::vector<double> power(n / 2);
    for (size_t i = 0; i < n / 2; ++i) power[i] = std::norm(x[i]);
    return power;
}
static double band(const std::vector<double>& s, double bin_hz, double lo, double hi) {
    double sum = 0;
    for (size_t i = 1; i < s.size(); ++i) if (i * bin_hz >= lo && i * bin_hz < hi) sum += s[i];
    return sum;
}
static double total(const std::vector<double>& s) { return band(s, 1, 1, 1e9); }
// Fraction of energy concentrated in the 20 strongest bins: a buzzer is near 1.
static double tonality(const std::vector<double>& s) {
    auto sorted = s;
    std::sort(sorted.begin(), sorted.end());
    double top = 0;
    for (size_t i = sorted.size() - 20; i < sorted.size(); ++i) top += sorted[i];
    return top / total(s);
}
// Energy near an engine order (multiple of crank rotation frequency).
static double order(const std::vector<double>& s, double bin_hz, double rpm, double k) {
    const double hz = rpm / 60 * k;
    return band(s, bin_hz, hz * 0.97, hz * 1.03);
}
static Pcm render(int car, float rpm, float throttle, unsigned rate, double seconds = 1.5,
                  float gain = 0.55f, float pan = 0.5f) {
    Pcm pcm(size_t(rate * seconds) * 2);
    Synth synth; Controls c;
    c[0].gain = gain; c[0].pan = pan; c[0].rpm = rpm; c[0].throttle = throttle; c[0].profile = car;
    synth.mix(pcm.data(), pcm.size() / 2, rate, c);
    return pcm;
}

// Audition: idle, full-throttle pulls with gear-ratio shift drops, lift-off
// overrun, part-throttle cruise and idle, for every car.
static void write_wav(const std::string& path, int car) {
    constexpr unsigned rate = 48000;
    const Profile& p = profile(car);
    const float ratios[] = {3.1f, 2.2f, 1.6f, 1.2f, 0.95f};
    std::vector<int16_t> pcm;
    Synth synth; Controls c;
    c[0].gain = 0.55f; c[0].profile = car;
    float rpm = p.idle_rpm, t = 0; int gear = 0;
    auto block = [&](float throttle) {
        c[0].rpm = rpm; c[0].throttle = throttle;
        const size_t at = pcm.size(); pcm.resize(at + 480 * 2);
        synth.mix(pcm.data() + at, 480, rate, c);
        t += 0.01f;
    };
    while (t < 2) block(0);
    for (int i = 0; i < 40; ++i) { rpm += 30; block(0.6f); } // blip
    while (rpm > p.idle_rpm + 20) { rpm -= 60; block(0); }
    while (t < 4) block(0);
    for (gear = 0; gear < 4; ++gear) {
        while (rpm < p.redline_rpm * 0.97f) { rpm += (p.redline_rpm - 800) * 0.01f / (1.0f + gear * 0.7f); block(1); }
        for (int i = 0; i < 12; ++i) block(0.1f);
        rpm *= ratios[gear + 1] / ratios[gear];
    }
    const float lift = t;
    while (t < lift + 3.5f) { rpm = std::max(p.idle_rpm + 1500, rpm - 25); block(0); }
    while (t < lift + 6.5f) { rpm += (3800 - rpm) * 0.05f; block(0.35f); }
    while (rpm > p.idle_rpm + 10) { rpm = std::max(p.idle_rpm, rpm - 40); block(0); }
    const float end = t;
    while (t < end + 1.5f) block(0);
    std::ofstream out(path, std::ios::binary);
    auto le = [&](uint32_t n, int bytes) { while (bytes--) { out.put(char(n & 255)); n >>= 8; } };
    out.write("RIFF", 4); le(36 + uint32_t(pcm.size()) * 2, 4); out.write("WAVEfmt ", 8);
    le(16, 4); le(1, 2); le(2, 2); le(rate, 4); le(rate * 4, 4); le(4, 2); le(16, 2);
    out.write("data", 4); le(uint32_t(pcm.size()) * 2, 4);
    for (auto s : pcm) le(uint16_t(s), 2);
    require(bool(out), "write audition WAV");
}

int main(int argc, char** argv) {
    if (argc == 2) {
        for (int car = 0; car < int(profile_count); ++car)
            write_wav(std::string(argv[1]) + "/engine-" + std::to_string(car) + "-" + profile(car).name + ".wav", car);
        return 0;
    }
    require(std::abs(rpm_from_pitch(0.1f + 7000 * 1.5f / 6900, 1100) - 7000) < 1,
            "pitch must invert the cartridge engine-speed formula");
    for (int car = 0; car < int(profile_count); ++car) {
        const Profile& p = profile(car);
        require(p.cylinders >= 6 && p.cylinders <= 12, "profile cylinder count");
        for (int i = 1; i < p.cylinders; ++i)
            require(p.firing_degrees[i] > p.firing_degrees[i - 1] && p.firing_degrees[i] < 720,
                    "firing angles are sorted within one cycle");
    }
    require(&profile(unknown_profile) == &profile(0) && &profile(99) == &profile(0),
            "unknown vehicles use the Diablo profile");

    for (unsigned rate : {22050u, 32000u, 48000u}) {
        const std::string at = " at " + std::to_string(rate) + " Hz";
        Controls c; Synth silent;
        Pcm pcm(rate * 2, 1234), original = pcm;
        silent.mix(pcm.data(), rate, rate, c);
        require(pcm == original, "inactive engines must preserve all original PCM" + at);

        c[0].gain = 0.55f; c[0].pan = 0; c[0].rpm = 3500; c[0].throttle = 0.8f; c[0].profile = 0;
        Synth a, b; Pcm whole(rate * 2), chunks(rate * 2);
        a.mix(whole.data(), rate, rate, c);
        for (size_t i = 0; i < rate;) {
            size_t n = std::min(size_t(137), size_t(rate) - i);
            b.mix(chunks.data() + 2 * i, n, rate, c); i += n;
        }
        require(whole == chunks, "buffer boundaries must not reset engine state" + at);
        require(energy(whole) > 10000, "engine must be audible" + at);
        require(energy(whole, 1) < energy(whole) * 0.02, "guest pan must control stereo placement" + at);

        c[0].gain = 0;
        std::fill(whole.begin(), whole.end(), 0); a.mix(whole.data(), rate, rate, c);
        require(std::all_of(whole.end() - 1000, whole.end(), [](auto s) { return s == 0; }),
                "stopped engines must fade to silence" + at);
        c[0] = {}; c[0].pitch = std::numeric_limits<float>::quiet_NaN(); c[0].gain = 1;
        c[0].rpm = std::numeric_limits<float>::infinity(); c[0].throttle = std::nanf("");
        a.mix(whole.data(), rate, rate, c); // Invalid control must remain bounded.

        // Pitch-only control (no vehicle sample) still follows the game's revs.
        Controls pc; pc[0].gain = 0.55f;
        pc[0].pitch = 0.1f + 3000 * 1.5f / 6900;
        Synth ps; Pcm low(rate * 3);
        ps.mix(low.data(), low.size() / 2, rate, pc);
        double hz; auto sl = spectrum(low, rate, hz);
        require(order(sl, hz, 3000, 6) > order(sl, hz, 3000, 4.8) * 3,
                "pitch-only control must fire at the V12 firing rate" + at);
    }

    constexpr unsigned rate = 32000;
    for (int car = 0; car < int(profile_count); ++car) {
        const Profile& p = profile(car);
        const std::string name = std::string(" (") + p.name + ")";
        const float high = p.redline_rpm * 0.9f;
        double hz;
        auto full_mid = render(car, 4000, 1, rate);
        auto s_mid = spectrum(full_mid, rate, hz);
        // The PR #438 synth measured 0.90 here: a near-pure harmonic buzz.
        require(tonality(s_mid) < 0.7, "engine must not be a pure tone" + name +
                " tonality=" + std::to_string(tonality(s_mid)));
        auto s_high = spectrum(render(car, high, 1, rate), rate, hz);
        require(band(s_high, hz, 20, 250) / total(s_high) > 0.04,
                "high revs must keep low-frequency body" + name);
        const double firing = p.cylinders / 2.0;
        // Per-hertz density: the firing line (6% wide) stands above the
        // broadband texture between it and the next half orders (20% wide).
        require(order(s_mid, hz, 4000, firing) / 0.06 > band(s_mid, hz, 4000.0 / 60 * firing * 1.15,
                4000.0 / 60 * firing * 1.35) / 0.20 * 3,
                "firing rate must remain the audible pitch" + name);
        // Cycle-to-cycle variation gives energy at half-orders of the cycle.
        require(band(s_mid, hz, 20, 4000.0 / 60 * firing * 0.9) / total(s_mid) > 0.08,
                "engine needs sub-firing-rate rumble" + name);

        auto lift = render(car, 4000, 0, rate);
        require(energy(full_mid, 0, rate) > energy(lift, 0, rate) * 3,
                "full throttle must be clearly louder than overrun" + name);
        auto s_lift = spectrum(lift, rate, hz);
        const double bright_full = band(s_mid, hz, 1500, 8000) / total(s_mid);
        const double bright_lift = band(s_lift, hz, 1500, 8000) / total(s_lift);
        require(bright_full > bright_lift * 1.3, "load must open up the exhaust tone" + name);
        // PR #438's mix balance was an RMS of about 650 at this gain; keep the
        // driven engine within roughly -2..+6 dB of it.
        const double rms = std::sqrt(energy(full_mid, 0, rate));
        require(rms > 520 && rms < 1300, "engine level must match the existing mix" + name +
                " rms=" + std::to_string(rms));
    }

    // Geometry: the Viper's 90-degree V10 alternates 90/54-degree firing
    // intervals; an even-fire 60-degree V12 cancels most of its half order.
    double hz;
    auto viper = spectrum(render(6, 3000, 1, rate, 2.5), rate, hz);
    auto diablo = spectrum(render(0, 3000, 1, rate, 2.5), rate, hz);
    const double viper_lump = order(viper, hz, 3000, 2.5) / order(viper, hz, 3000, 5);
    const double v12_lump = order(diablo, hz, 3000, 3) / order(diablo, hz, 3000, 6);
    require(viper_lump > v12_lump * 2, "uneven V10 firing must be audible as a lumpy half order");
    require(profile(6).cylinders == 10 && profile(1).cylinders == 6 && profile(0).cylinders == 12,
            "car layouts: Viper V10, 959 flat-six, Diablo V12");

    // Turbo: lifting after boost builds releases a blow-off hiss the Diablo lacks.
    auto blowoff = [&](int car) {
        Pcm pcm(rate * 4 * 2);
        Synth s; Controls c; c[0].gain = 0.55f; c[0].profile = car; c[0].rpm = 5500;
        c[0].throttle = 1;
        s.mix(pcm.data(), rate * 3, rate, c);
        c[0].throttle = 0;
        s.mix(pcm.data() + rate * 3 * 2, rate, rate, c);
        Pcm tail(pcm.begin() + rate * 3 * 2, pcm.begin() + rate * 3 * 2 + rate / 2 * 2);
        double h; auto sp = spectrum(Pcm(tail.begin(), tail.end()), rate, h);
        return band(sp, h, 1500, 6000) / total(sp);
    };
    require(blowoff(1) > blowoff(0) * 1.5, "959 turbo must hiss on lift-off");
    std::cout << "engine audio tests passed\n";
}
