#pragma once
// Host-visible parameters for MixCore, mapped 1:1 onto the engine settings
// (mixcore::gain::SmartGainSettings, mixcore::gate::GateSettings and
// mixcore::drum::DrumSettings). Ranges
// and defaults come from the engine schema so there is a single source of
// truth; tests assert they match.

#include "mixcore/strip/ChannelStrip.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace mixcore::plugin {

namespace ids {
inline constexpr const char* targetRms = "targetRms";
inline constexpr const char* staticGain = "staticGain";
inline constexpr const char* riderEnabled = "riderEnabled";
inline constexpr const char* riderStrength = "riderStrength";
inline constexpr const char* riderRange = "riderRange";
inline constexpr const char* riderMaxUp = "riderMaxUp";
inline constexpr const char* riderMaxDown = "riderMaxDown";
inline constexpr const char* riderAttack = "riderAttack";
inline constexpr const char* riderRelease = "riderRelease";
inline constexpr const char* riderMaxRate = "riderMaxRate";
inline constexpr const char* deadband = "deadband";
inline constexpr const char* idleDelay = "idleDelay";
inline constexpr const char* idleRate = "idleRate";
inline constexpr const char* activityConfirm = "activityConfirm";
inline constexpr const char* phraseHold = "phraseHold";
inline constexpr const char* activityMargin = "activityMargin";
inline constexpr const char* detectorHpf = "detectorHpf";
inline constexpr const char* peakWarning = "peakWarning";
inline constexpr const char* peakRestrict = "peakRestrict";
inline constexpr const char* peakGuard = "peakGuard";
inline constexpr const char* guardCeiling = "guardCeiling";
inline constexpr const char* bypass = "bypass";

inline constexpr const char* gateEnabled = "gateEnabled";
inline constexpr const char* gateThreshold = "gateThreshold";
inline constexpr const char* gateRange = "gateRange";
inline constexpr const char* gateRatio = "gateRatio";
inline constexpr const char* gateHysteresis = "gateHysteresis";
inline constexpr const char* gateAttack = "gateAttack";
inline constexpr const char* gateHold = "gateHold";
inline constexpr const char* gateRelease = "gateRelease";
inline constexpr const char* gateScHpf = "gateScHpf";
inline constexpr const char* gateScLpf = "gateScLpf";
inline constexpr const char* gateRms = "gateRms";
inline constexpr const char* gateTransient = "gateTransient";
inline constexpr const char* gateTransientDb = "gateTransientDb";
inline constexpr const char* gateLookahead = "gateLookahead";

inline constexpr const char* smartGainOn = "smartGainOn";
inline constexpr const char* sourceType = "sourceType";

inline constexpr const char* drumOn = "drumOn";
inline constexpr const char* drumDebleed = "drumDebleed";
inline constexpr const char* drumReplace = "drumReplace";
inline constexpr const char* replaceThreshold = "replaceThreshold";
inline constexpr const char* replaceRetrigger = "replaceRetrigger";
inline constexpr const char* replaceDynamics = "replaceDynamics";
inline constexpr const char* replaceRange = "replaceRange";
inline constexpr const char* replaceMix = "replaceMix";
inline constexpr const char* replaceSample = "replaceSample";
inline constexpr const char* replaceOriginal = "replaceOriginal";
inline constexpr const char* replacePitch = "replacePitch";
inline constexpr const char* replaceAttack = "replaceAttack";
inline constexpr const char* replaceLength = "replaceLength";
inline constexpr const char* replaceLookahead = "replaceLookahead";
inline constexpr const char* replaceDetectLow = "replaceDetectLow";
inline constexpr const char* replaceDetectHigh = "replaceDetectHigh";
inline constexpr const char* drumDebleedAmount = "drumDebleedAmount";
inline constexpr const char* drumDebleedCrossover = "drumDebleedCrossover";
inline constexpr const char* drumDebleedLength = "drumDebleedLength";
inline constexpr const char* drumDebleedTolerance = "drumDebleedTolerance";
} // namespace ids

template <typename Settings>
struct FloatSpec
{
    const char* id;
    const char* name;
    float Settings::*member;
    float min;
    float max;
    float step;
    float skewCentre; ///< 0 (or outside the range) means linear.
    const char* unit;
    int decimals;
    bool advanced;    ///< Shown only in Advanced mode.
    bool automatable = true;
};

template <typename Settings>
struct BoolSpec
{
    const char* id;
    const char* name;
    bool Settings::*member;
    bool automatable;
};

using FloatParamSpec = FloatSpec<gain::SmartGainSettings>;
using BoolParamSpec = BoolSpec<gain::SmartGainSettings>;
using GateFloatParamSpec = FloatSpec<gate::GateSettings>;
using GateBoolParamSpec = BoolSpec<gate::GateSettings>;
using DrumFloatParamSpec = FloatSpec<drum::DrumSettings>;
using DrumBoolParamSpec = BoolSpec<drum::DrumSettings>;

const std::vector<FloatParamSpec>& floatParamSpecs();
const std::vector<BoolParamSpec>& boolParamSpecs();
const std::vector<GateFloatParamSpec>& gateFloatParamSpecs();
const std::vector<GateBoolParamSpec>& gateBoolParamSpecs();
const std::vector<DrumFloatParamSpec>& drumFloatParamSpecs();
const std::vector<DrumBoolParamSpec>& drumBoolParamSpecs();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/// Reads all parameters into a settings struct. Construct on the message
/// thread; read() is realtime-safe (atomic loads only, no allocation).
class ParameterReader
{
public:
    explicit ParameterReader(juce::AudioProcessorValueTreeState& state);
    gain::SmartGainSettings read() const noexcept;
    gate::GateSettings readGate() const noexcept;
    drum::DrumSettings readDrum() const noexcept;
    strip::ChannelSettings readChannel() const noexcept
    {
        strip::ChannelSettings c;
        c.gainOn = gainOn_->load(std::memory_order_relaxed) >= 0.5f;
        c.source = gain::sourceFromIndex(static_cast<int>(source_->load(std::memory_order_relaxed) + 0.5f));
        c.gain = read();
        c.gate = readGate();
        c.drum = readDrum();
        return c;
    }

private:
    std::vector<std::pair<std::atomic<float>*, float gain::SmartGainSettings::*>> floats_;
    std::vector<std::pair<std::atomic<float>*, bool gain::SmartGainSettings::*>> bools_;
    std::vector<std::pair<std::atomic<float>*, float gate::GateSettings::*>> gateFloats_;
    std::vector<std::pair<std::atomic<float>*, bool gate::GateSettings::*>> gateBools_;
    std::atomic<float>* gainOn_ = nullptr;
    std::atomic<float>* source_ = nullptr;
    std::vector<std::pair<std::atomic<float>*, float drum::DrumSettings::*>> drumFloats_;
    std::vector<std::pair<std::atomic<float>*, bool drum::DrumSettings::*>> drumBools_;
};

} // namespace mixcore::plugin
