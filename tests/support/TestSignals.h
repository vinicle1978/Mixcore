#pragma once
// Deterministic synthetic test signals and helpers.
//
// "Vocal-like" here means a harmonic tone with slow amplitude modulation and
// vibrato. It exercises level detection, activity and riding deterministically;
// it is NOT a substitute for listening tests with real recordings (see
// docs/testing/test-plan.md).

#include "mixcore/core/DbMath.h"
#include "mixcore/gain/SmartGainStage.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace mixcore::test {

constexpr double kPi = 3.14159265358979323846;

class Rng
{
public:
    explicit Rng(std::uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }
    /// Uniform in [-1, 1).
    float bipolar() noexcept { return static_cast<float>((next() >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0); }
    float uniform() noexcept { return 0.5f * (bipolar() + 1.0f); }

private:
    std::uint64_t state_;
};

using Signal = std::vector<float>;

inline double rmsDb(const Signal& s, size_t begin = 0, size_t end = SIZE_MAX)
{
    end = std::min(end, s.size());
    double acc = 0.0;
    for (size_t i = begin; i < end; ++i)
        acc += static_cast<double>(s[i]) * static_cast<double>(s[i]);
    return static_cast<double>(powerToDb(end > begin ? acc / static_cast<double>(end - begin) : 0.0));
}

inline void scaleToRmsDb(Signal& s, double targetDb)
{
    const double current = rmsDb(s);
    const float g = static_cast<float>(std::pow(10.0, (targetDb - current) / 20.0));
    for (auto& v : s)
        v *= g;
}

inline Signal sine(double sr, double freq, double seconds, double amplitude, double phase = 0.0)
{
    Signal s(static_cast<size_t>(seconds * sr));
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * freq * static_cast<double>(i) / sr + phase));
    return s;
}

inline Signal noise(double sr, double seconds, double rmsDbValue, std::uint64_t seed)
{
    Rng rng(seed);
    Signal s(static_cast<size_t>(seconds * sr));
    for (auto& v : s)
        v = rng.bipolar();
    scaleToRmsDb(s, rmsDbValue);
    return s;
}

/// Harmonic "sung" tone with 4 Hz AM (+/-1.5 dB), 5.5 Hz vibrato and
/// 20 ms fade in/out, scaled to the requested RMS.
inline Signal vocalPhrase(double sr, double seconds, double rmsDbValue, double f0 = 196.0)
{
    Signal s(static_cast<size_t>(seconds * sr));
    double phase = 0.0;
    const size_t fade = static_cast<size_t>(0.02 * sr);
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const double f = f0 * (1.0 + 0.01 * std::sin(2.0 * kPi * 5.5 * t));
        phase += 2.0 * kPi * f / sr;
        double v = 0.0;
        for (int k = 1; k <= 6; ++k)
            v += std::sin(k * phase) / k;
        const double am = std::pow(10.0, 1.5 * std::sin(2.0 * kPi * 4.0 * t) / 20.0);
        double env = 1.0;
        if (i < fade)
            env = static_cast<double>(i) / fade;
        else if (i + fade > s.size())
            env = static_cast<double>(s.size() - i) / fade;
        s[i] = static_cast<float>(v * am * env);
    }
    scaleToRmsDb(s, rmsDbValue);
    return s;
}

struct Segment
{
    enum Kind { Vocal, Silence } kind;
    double seconds;
    double rmsDb = -150.0;
};

/// Concatenate segments and add a continuous room-noise bed (if noiseDb > -140).
inline Signal compose(double sr, const std::vector<Segment>& segments, double noiseDb = -150.0, std::uint64_t seed = 1)
{
    Signal out;
    for (const auto& seg : segments)
    {
        if (seg.kind == Segment::Vocal)
        {
            auto p = vocalPhrase(sr, seg.seconds, seg.rmsDb);
            out.insert(out.end(), p.begin(), p.end());
        }
        else
        {
            out.insert(out.end(), static_cast<size_t>(seg.seconds * sr), 0.0f);
        }
    }
    if (noiseDb > -140.0)
    {
        auto n = noise(sr, static_cast<double>(out.size()) / sr + 0.01, noiseDb, seed);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] += n[i];
    }
    return out;
}

/// Process a mono or multichannel signal through `stage` in blocks.
/// `onBlock` (optional) is called after each block with the sample index at
/// the end of the block.
inline void runStage(gain::SmartGainStage& stage, std::vector<Signal>& channels, int blockSize,
                     const std::function<void(size_t)>& onBlock = {})
{
    const size_t total = channels.front().size();
    std::vector<float*> ptrs(channels.size());
    for (size_t pos = 0; pos < total; pos += static_cast<size_t>(blockSize))
    {
        const int n = static_cast<int>(std::min(static_cast<size_t>(blockSize), total - pos));
        for (size_t ch = 0; ch < channels.size(); ++ch)
            ptrs[ch] = channels[ch].data() + pos;
        stage.process(ptrs.data(), static_cast<int>(channels.size()), n);
        if (onBlock)
            onBlock(pos + static_cast<size_t>(n));
    }
}

/// Convenience: mono processing, returns the processed signal.
inline Signal processMono(gain::SmartGainStage& stage, const Signal& in, int blockSize,
                          const std::function<void(size_t)>& onBlock = {})
{
    std::vector<Signal> ch { in };
    runStage(stage, ch, blockSize, onBlock);
    return ch.front();
}

} // namespace mixcore::test
