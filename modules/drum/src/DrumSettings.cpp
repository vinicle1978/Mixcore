#include "mixcore/drum/DrumSettings.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace mixcore::drum {

namespace {
struct FloatField
{
    const char* key;
    float DrumSettings::*member;
    float lo;
    float hi;
};
struct BoolField
{
    const char* key;
    bool DrumSettings::*member;
};
constexpr FloatField kFloatFields[] = {
    { "debleedAmount", &DrumSettings::debleedAmount, 0.0f, 100.0f },
    { "debleedCrossoverHz", &DrumSettings::debleedCrossoverHz, 1000.0f, 12000.0f },
    { "debleedLengthMs", &DrumSettings::debleedLengthMs, 10.0f, 1000.0f },
    { "debleedToleranceDb", &DrumSettings::debleedToleranceDb, 0.0f, 12.0f },
    { "replaceThresholdDb", &DrumSettings::replaceThresholdDb, -60.0f, 0.0f },
    { "replaceRetriggerMs", &DrumSettings::replaceRetriggerMs, 5.0f, 500.0f },
    { "replaceDynamics", &DrumSettings::replaceDynamics, 0.0f, 100.0f },
    { "replaceRangeDb", &DrumSettings::replaceRangeDb, 6.0f, 48.0f },
    { "replaceMix", &DrumSettings::replaceMix, 0.0f, 100.0f },
    { "replaceSampleDb", &DrumSettings::replaceSampleDb, -40.0f, 12.0f },
    { "replaceOriginalDb", &DrumSettings::replaceOriginalDb, -60.0f, 12.0f },
    { "replacePitchSt", &DrumSettings::replacePitchSt, -12.0f, 12.0f },
    { "replaceAttackMs", &DrumSettings::replaceAttackMs, 0.0f, 30.0f },
    { "replaceLengthMs", &DrumSettings::replaceLengthMs, 20.0f, 2000.0f },
    { "replaceLookaheadMs", &DrumSettings::replaceLookaheadMs, 0.5f, 10.0f },
    { "replaceDetectLowHz", &DrumSettings::replaceDetectLowHz, 20.0f, 2000.0f },
    { "replaceDetectHighHz", &DrumSettings::replaceDetectHighHz, 100.0f, 20000.0f },
};
constexpr BoolField kBoolFields[] = {
    { "moduleOn", &DrumSettings::moduleOn },
    { "debleedEnabled", &DrumSettings::debleedEnabled },
    { "replaceEnabled", &DrumSettings::replaceEnabled },
};
} // namespace

void DrumSettings::validate() noexcept
{
    const DrumSettings defaults;
    for (const auto& f : kFloatFields)
        this->*f.member = clampFinite(this->*f.member, f.lo, f.hi, defaults.*f.member);
}

bool approximatelyEqual(const DrumSettings& a, const DrumSettings& b, float tolerance) noexcept
{
    for (const auto& f : kBoolFields)
        if (a.*f.member != b.*f.member)
            return false;
    for (const auto& f : kFloatFields)
    {
        const float x = a.*f.member, y = b.*f.member;
        if (std::abs(x - y) > tolerance * std::max(1.0f, std::abs(y)))
            return false;
    }
    return true;
}

std::string DrumSettings::serialise() const
{
    std::ostringstream out;
    out.precision(9);
    out << "version=2;";
    for (const auto& f : kFloatFields)
        out << f.key << '=' << this->*f.member << ';';
    for (const auto& b : kBoolFields)
        out << b.key << '=' << (this->*b.member ? 1 : 0) << ';';
    return out.str();
}

DrumSettings DrumSettings::deserialise(const std::string& text)
{
    DrumSettings s;
    bool sawModuleOn = false;
    std::istringstream in(text);
    std::string pair;
    while (std::getline(in, pair, ';'))
    {
        const auto eq = pair.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = pair.substr(0, eq);
        const std::string value = pair.substr(eq + 1);
        char* end = nullptr;
        const float parsed = std::strtof(value.c_str(), &end);
        if (end == value.c_str())
            continue;
        for (const auto& f : kFloatFields)
            if (key == f.key)
                s.*f.member = parsed;
        for (const auto& b : kBoolFields)
            if (key == b.key)
                s.*b.member = parsed >= 0.5f;
        if (key == "moduleOn")
            sawModuleOn = true;
    }
    // Version 1 files had no master switch: the module was on if de-bleed was.
    if (!sawModuleOn)
        s.moduleOn = s.debleedEnabled || s.replaceEnabled;
    s.validate();
    return s;
}

} // namespace mixcore::drum
