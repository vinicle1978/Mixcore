#include "mixcore/gain/SmartGainSettings.h"

#include "mixcore/core/DbMath.h"

#include <cstdlib>
#include <sstream>

namespace mixcore::gain {

namespace {

struct FloatField
{
    const char* key;
    float SmartGainSettings::*member;
    float lo;
    float hi;
};

struct BoolField
{
    const char* key;
    bool SmartGainSettings::*member;
};

constexpr FloatField kFloatFields[] = {
    { "targetRmsDb", &SmartGainSettings::targetRmsDb, -40.0f, -6.0f },
    { "staticGainDb", &SmartGainSettings::staticGainDb, -18.0f, 18.0f },
    { "riderStrength", &SmartGainSettings::riderStrength, 0.0f, 1.0f },
    { "riderRangeDb", &SmartGainSettings::riderRangeDb, 0.0f, 12.0f },
    { "riderMaxUpDb", &SmartGainSettings::riderMaxUpDb, 0.0f, 12.0f },
    { "riderMaxDownDb", &SmartGainSettings::riderMaxDownDb, 0.0f, 12.0f },
    { "riderAttackMs", &SmartGainSettings::riderAttackMs, 10.0f, 2000.0f },
    { "riderReleaseMs", &SmartGainSettings::riderReleaseMs, 10.0f, 5000.0f },
    { "riderMaxRateDbPerSec", &SmartGainSettings::riderMaxRateDbPerSec, 0.5f, 60.0f },
    { "deadbandDb", &SmartGainSettings::deadbandDb, 0.0f, 6.0f },
    { "idleReleaseDelayMs", &SmartGainSettings::idleReleaseDelayMs, 250.0f, 10000.0f },
    { "idleReleaseDbPerSec", &SmartGainSettings::idleReleaseDbPerSec, 0.0f, 6.0f },
    { "activityConfirmMs", &SmartGainSettings::activityConfirmMs, 0.0f, 500.0f },
    { "phraseHoldMs", &SmartGainSettings::phraseHoldMs, 0.0f, 2000.0f },
    { "activityMarginDb", &SmartGainSettings::activityMarginDb, 3.0f, 30.0f },
    { "peakWarningDb", &SmartGainSettings::peakWarningDb, -24.0f, 0.0f },
    { "peakRestrictDb", &SmartGainSettings::peakRestrictDb, -24.0f, 0.0f },
    { "peakGuardCeilingDb", &SmartGainSettings::peakGuardCeilingDb, -24.0f, 0.0f },
};

constexpr BoolField kBoolFields[] = {
    { "riderEnabled", &SmartGainSettings::riderEnabled },
    { "peakGuardEnabled", &SmartGainSettings::peakGuardEnabled },
    { "bypass", &SmartGainSettings::bypass },
};

} // namespace

void SmartGainSettings::validate() noexcept
{
    const SmartGainSettings defaults;
    for (const auto& f : kFloatFields)
        this->*f.member = clampFinite(this->*f.member, f.lo, f.hi, defaults.*f.member);
}

std::string SmartGainSettings::serialise() const
{
    std::ostringstream out;
    out.precision(9);
    out << "version=1;";
    for (const auto& f : kFloatFields)
        out << f.key << '=' << this->*f.member << ';';
    for (const auto& b : kBoolFields)
        out << b.key << '=' << (this->*b.member ? 1 : 0) << ';';
    return out.str();
}

SmartGainSettings SmartGainSettings::deserialise(const std::string& text)
{
    SmartGainSettings s;
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
            continue; // not a number: keep default

        for (const auto& f : kFloatFields)
            if (key == f.key)
                s.*f.member = parsed;
        for (const auto& b : kBoolFields)
            if (key == b.key)
                s.*b.member = parsed >= 0.5f;
    }
    s.validate();
    return s;
}

} // namespace mixcore::gain
