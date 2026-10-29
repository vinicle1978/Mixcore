#pragma once
// Host-visible parameters for MixCore, mapped 1:1 onto
// mixcore::gain::SmartGainSettings. Ranges and defaults come from the engine
// schema so there is a single source of truth.

#include "mixcore/gain/SmartGainSettings.h"

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
inline constexpr const char* peakWarning = "peakWarning";
inline constexpr const char* peakRestrict = "peakRestrict";
inline constexpr const char* peakGuard = "peakGuard";
inline constexpr const char* guardCeiling = "guardCeiling";
inline constexpr const char* bypass = "bypass";
} // namespace ids

struct FloatParamSpec
{
    const char* id;
    const char* name;
    float gain::SmartGainSettings::*member;
    float min;
    float max;
    float step;
    float skewCentre; ///< 0 (or outside the range) means linear.
    const char* unit;
    int decimals;
    bool advanced;    ///< Shown only in Advanced mode.
};

struct BoolParamSpec
{
    const char* id;
    const char* name;
    bool gain::SmartGainSettings::*member;
    bool automatable;
};

const std::vector<FloatParamSpec>& floatParamSpecs();
const std::vector<BoolParamSpec>& boolParamSpecs();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/// Reads all parameters into a settings struct. Construct on the message
/// thread; read() is realtime-safe (atomic loads only, no allocation).
class ParameterReader
{
public:
    explicit ParameterReader(juce::AudioProcessorValueTreeState& state);
    gain::SmartGainSettings read() const noexcept;

private:
    std::vector<std::pair<std::atomic<float>*, float gain::SmartGainSettings::*>> floats_;
    std::vector<std::pair<std::atomic<float>*, bool gain::SmartGainSettings::*>> bools_;
};

} // namespace mixcore::plugin
