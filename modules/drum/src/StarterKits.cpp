#include "mixcore/drum/StarterKits.h"

#include "mixcore/dsp/Biquad.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mixcore::drum {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Rng
{
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 1) {}
    double bipolar()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53) * 2.0 - 1.0;
    }
};

void normalise(std::vector<float>& x, float peakTarget)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max(p, std::abs(v));
    if (p > 0.0f)
        for (float& v : x)
            v *= peakTarget / p;
}

/// Kick: pitch-swept sine body, beater click, a little noise. v = 0..1 (hit strength).
std::vector<float> kick(double sr, double v, std::uint64_t seed)
{
    Rng rng(seed);
    const double decay = 0.26 + 0.08 * v + 0.01 * rng.bipolar();
    const double fBase = 46.0 + 1.5 * rng.bipolar();
    const auto n = static_cast<size_t>(0.7 * sr);
    std::vector<float> x(n);
    double phase = 0.0;
    dsp::Biquad hp;
    hp.setHighPass(sr, 1500.0, 0.7071);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const double f = fBase + (70.0 + 80.0 * v) * std::exp(-t / 0.032);
        phase += 2.0 * kPi * f / sr;
        const double body = std::sin(phase) * std::exp(-t / decay);
        const double beater = (0.15 + 0.35 * v) * std::sin(2.0 * kPi * 2800.0 * t) * std::exp(-t / 0.0012);
        const double noise = (0.1 + 0.3 * v) * static_cast<double>(hp.process(static_cast<float>(rng.bipolar())))
                             * std::exp(-t / 0.004);
        x[i] = static_cast<float>(body + beater + noise);
    }
    normalise(x, static_cast<float>(0.9 * v));
    return x;
}

/// Snare: two drum-head modes plus filtered noise "wires".
std::vector<float> snare(double sr, double v, std::uint64_t seed)
{
    Rng rng(seed);
    const auto n = static_cast<size_t>(0.55 * sr);
    std::vector<float> x(n);
    dsp::Biquad hp, lp1, lp2;
    hp.setHighPass(sr, 1200.0, 0.7071);
    lp1.setLowPass(sr, 4500.0 + 6000.0 * v, 0.7071); // harder hits are brighter
    lp2.setLowPass(sr, 4500.0 + 6000.0 * v, 0.7071);
    const double f1 = 182.0 + 3.0 * rng.bipolar();
    const double wireDecay = 0.13 + 0.05 * v + 0.01 * rng.bipolar();
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const double body = std::sin(2.0 * kPi * f1 * t) * std::exp(-t / 0.09)
                            + 0.5 * std::sin(2.0 * kPi * (f1 * 1.78) * t) * std::exp(-t / 0.05);
        float w = static_cast<float>(rng.bipolar());
        w = lp2.process(lp1.process(hp.process(w)));
        const double wires = (0.6 + 0.6 * v) * static_cast<double>(w) * std::exp(-t / wireDecay);
        const double stick = (0.2 + 0.4 * v) * rng.bipolar() * std::exp(-t / 0.0015);
        x[i] = static_cast<float>(0.8 * body + 1.6 * wires + stick);
    }
    normalise(x, static_cast<float>(0.9 * v));
    return x;
}

/// Tom: lower, longer pitch-swept body with a soft attack.
std::vector<float> tom(double sr, double v, std::uint64_t seed)
{
    Rng rng(seed);
    const auto n = static_cast<size_t>(0.9 * sr);
    std::vector<float> x(n);
    double phase = 0.0;
    const double base = 98.0 + 2.0 * rng.bipolar();
    dsp::Biquad lp;
    lp.setLowPass(sr, 3000.0 + 3000.0 * v, 0.7071);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const double f = base + (35.0 + 25.0 * v) * std::exp(-t / 0.06);
        phase += 2.0 * kPi * f / sr;
        const double body = std::sin(phase) * std::exp(-t / (0.32 + 0.08 * v));
        const double attack = (0.15 + 0.3 * v) * static_cast<double>(lp.process(static_cast<float>(rng.bipolar())))
                              * std::exp(-t / 0.006);
        x[i] = static_cast<float>(body + attack);
    }
    normalise(x, static_cast<float>(0.9 * v));
    return x;
}

} // namespace

const std::vector<StarterKitInfo>& starterKits()
{
    static const std::vector<StarterKitInfo> kits = {
        { "starter:kick", "Starter Kick" },
        { "starter:snare", "Starter Snare" },
        { "starter:tom", "Starter Tom" },
    };
    return kits;
}

std::unique_ptr<SampleKit> makeStarterKit(const std::string& id, double sampleRate)
{
    std::vector<float> (*make)(double, double, std::uint64_t) = nullptr;
    std::string name;
    for (const auto& k : starterKits())
        if (id == k.id)
            name = k.name;
    if (id == "starter:kick")
        make = &kick;
    else if (id == "starter:snare")
        make = &snare;
    else if (id == "starter:tom")
        make = &tom;
    if (make == nullptr)
        return nullptr;

    // Layers are built explicitly (3 strengths x 2 variations), not by loudness
    // grouping: brighter hard hits can measure close to softer ones.
    auto kit = std::make_unique<SampleKit>();
    kit->id = id;
    kit->name = name;
    std::uint64_t seed = 1;
    for (double v : { 0.3, 0.6, 1.0 })
    {
        SampleLayer layer;
        double sum = 0.0;
        for (int rr = 0; rr < 2; ++rr)
        {
            SampleBuffer b;
            b.name = name + " v" + std::to_string(static_cast<int>(v * 100)) + " rr" + std::to_string(rr + 1);
            b.sampleRate = sampleRate;
            b.left = make(sampleRate, v, seed++);
            analyse(b);
            sum += static_cast<double>(b.loudnessDb);
            layer.roundRobin.push_back(std::move(b));
        }
        layer.loudnessDb = static_cast<float>(sum / 2.0);
        kit->layers.push_back(std::move(layer));
    }
    return kit;
}

} // namespace mixcore::drum
