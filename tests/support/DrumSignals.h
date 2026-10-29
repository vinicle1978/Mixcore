#pragma once
// Deterministic synthetic drum signals for gain and gate tests. Like the
// synthetic vocal, they exercise detection deterministically and are not a
// substitute for listening tests with real drum recordings.

#include "TestSignals.h"

#include <cmath>
#include <cstdint>

namespace mixcore::test {

/// Synthetic kick: 150 -> 50 Hz pitch sweep with a 120 ms decay plus a short
/// click, repeated at `bpm` quarter notes, over continuous room noise/bleed.
inline Signal kickGroove(double sr, double seconds, double bpm, float peak, double bleedDb, std::uint64_t seed)
{
    Signal s(static_cast<size_t>(seconds * sr), 0.0f);
    const auto period = static_cast<size_t>(60.0 / bpm * sr);
    for (size_t start = static_cast<size_t>(0.25 * sr); start < s.size(); start += period)
    {
        double phase = 0.0;
        for (size_t k = 0; k < static_cast<size_t>(0.4 * sr) && start + k < s.size(); ++k)
        {
            const double t = static_cast<double>(k) / sr;
            const double f = 50.0 + 100.0 * std::exp(-t / 0.03);
            phase += 2.0 * kPi * f / sr;
            const double body = std::exp(-t / 0.12) * std::sin(phase);
            const double click = k < 48 ? 0.3 * std::exp(-t / 0.0005) : 0.0;
            s[start + k] += static_cast<float>(static_cast<double>(peak) * (body + click));
        }
    }
    const auto bleed = noise(sr, seconds + 0.01, bleedDb, seed);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] += bleed[i];
    return s;
}

/// Synthetic snare: noisy burst (200 Hz body + broadband wires) with 90 ms decay
/// on beats 2 and 4.
inline Signal snareGroove(double sr, double seconds, double bpm, float peak, double bleedDb, std::uint64_t seed)
{
    Signal s(static_cast<size_t>(seconds * sr), 0.0f);
    Rng rng(seed);
    const auto bar = static_cast<size_t>(4.0 * 60.0 / bpm * sr);
    const auto beat = bar / 4;
    for (size_t barStart = static_cast<size_t>(0.25 * sr); barStart < s.size(); barStart += bar)
        for (size_t hit : { barStart + beat, barStart + 3 * beat })
            for (size_t k = 0; k < static_cast<size_t>(0.3 * sr) && hit + k < s.size(); ++k)
            {
                const double t = static_cast<double>(k) / sr;
                const double env = std::exp(-t / 0.09);
                const double body = 0.6 * std::sin(2.0 * kPi * 200.0 * t);
                s[hit + k] += static_cast<float>(static_cast<double>(peak) * env
                                                 * (body + 0.6 * static_cast<double>(rng.bipolar())));
            }
    const auto bleed = noise(sr, seconds + 0.01, bleedDb, seed + 1);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] += bleed[i];
    return s;
}

} // namespace mixcore::test
