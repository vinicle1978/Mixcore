#pragma once
// Gate snapshot, published every 10 ms through a TripleBuffer.

#include <cstdint>

namespace mixcore::gate {

enum class GateState : std::uint8_t
{
    Closed = 0,  ///< Below threshold, attenuating (or releasing towards full range).
    Open = 1,    ///< Above threshold.
    Hold = 2     ///< Level dropped below close threshold; held open for holdMs.
};

enum class LearnState : std::uint8_t
{
    Idle = 0,
    Requested = 1,
    Listening = 2,
    Ready = 3
};

struct GateTelemetry
{
    std::uint64_t sequence = 0;
    float detectorDb = -150.0f;       ///< Max detector level in the last tick (side-chain filtered).
    float thresholdDb = -40.0f;       ///< Settings echo, for drawing.
    float closeThresholdDb = -46.0f;  ///< threshold - hysteresis.
    float gainReductionDb = 0.0f;     ///< Max attenuation applied in the last tick (>= 0).
    float outputRmsDb = -150.0f;      ///< Gate output, 400 ms RMS (= channel output).
    float outputPeakDb = -150.0f;     ///< Gate output sample peak, last 50 ms.
    GateState state = GateState::Closed;
    bool enabled = false;
    bool hardGate = true;
    std::uint32_t opensTotal = 0;     ///< Closed->Open transitions since reset.
    std::uint16_t opensLastSecond = 0;///< For chatter detection.
    float closedSeconds = 0.0f;       ///< How long the gate has been continuously closed.
    LearnState learn = LearnState::Idle;
    float learnProgress = 0.0f;
    int latencySamples = 0;
};

} // namespace mixcore::gate
