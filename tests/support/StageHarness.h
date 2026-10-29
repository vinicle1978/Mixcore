#pragma once
// Helpers to build a SmartGainStage and record its telemetry tick by tick.

#include "TestSignals.h"

#include "mixcore/gain/SmartGainStage.h"

#include <memory>
#include <vector>

namespace mixcore::test {

inline std::unique_ptr<gain::SmartGainStage> makeStage(double sr, const gain::SmartGainSettings& s, int channels = 1,
                                                       int maxBlock = 4096)
{
    auto stage = std::make_unique<gain::SmartGainStage>();
    stage->prepare(sr, maxBlock, channels);
    stage->setSettings(s);
    stage->reset(); // start settled with these settings (no initial ramps)
    return stage;
}

inline int hopFor(double sr) { return msToSamples(gain::SmartGainStage::kControlIntervalMs, sr); }

/// Process mono `in` in hop-sized blocks and capture one telemetry snapshot per
/// control tick. Returns the output signal.
inline Signal processWithTelemetry(gain::SmartGainStage& stage, const Signal& in,
                                   std::vector<gain::SmartGainTelemetry>& ticks)
{
    const int hop = hopFor(stage.sampleRate());
    ticks.clear();
    ticks.reserve(in.size() / static_cast<size_t>(hop) + 1);
    return processMono(stage, in, hop, [&](size_t) {
        gain::SmartGainTelemetry t;
        if (stage.readTelemetry(t))
            ticks.push_back(t);
    });
}

inline size_t tickAt(double seconds) { return static_cast<size_t>(seconds * 100.0) - 1; }

} // namespace mixcore::test
