#include "mixcore/gate/GateSettings.h"

#include "mixcore/core/DbMath.h"

#include <cstdlib>
#include <sstream>

namespace mixcore::gate {

namespace {
struct FloatField
{
    const char* key;
    float GateSettings::*member;
    float lo;
    float hi;
};
struct BoolField
{
    const char* key;
    bool GateSettings::*member;
};

constexpr FloatField kFloatFields[] = {
    { "thresholdDb", &GateSettings::thresholdDb, -80.0f, 0.0f },
    { "rangeDb", &GateSettings::rangeDb, 0.0f, 80.0f },
    { "ratio", &GateSettings::ratio, 1.2f, kHardGateRatio },
    { "hysteresisDb", &GateSettings::hysteresisDb, 0.0f, 20.0f },
    { "attackMs", &GateSettings::attackMs, 0.05f, 50.0f },
    { "holdMs", &GateSettings::holdMs, 0.0f, 2000.0f },
    { "releaseMs", &GateSettings::releaseMs, 5.0f, 4000.0f },
    { "sidechainHpfHz", &GateSettings::sidechainHpfHz, 20.0f, 2000.0f },
    { "sidechainLpfHz", &GateSettings::sidechainLpfHz, 500.0f, 20000.0f },
    { "transientDb", &GateSettings::transientDb, 1.0f, 24.0f },
    { "lookaheadMs", &GateSettings::lookaheadMs, 0.0f, 5.0f },
};
constexpr BoolField kBoolFields[] = {
    { "enabled", &GateSettings::enabled },
    { "rmsDetector", &GateSettings::rmsDetector },
    { "transientTrigger", &GateSettings::transientTrigger },
};
} // namespace

void GateSettings::validate() noexcept
{
    const GateSettings defaults;
    for (const auto& f : kFloatFields)
        this->*f.member = clampFinite(this->*f.member, f.lo, f.hi, defaults.*f.member);
}

std::string GateSettings::serialise() const
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

GateSettings GateSettings::deserialise(const std::string& text)
{
    GateSettings s;
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
    }
    s.validate();
    return s;
}

} // namespace mixcore::gate
