#include "mixcore/drum/DrumSettings.h"

#include "mixcore/core/DbMath.h"

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
};
constexpr BoolField kBoolFields[] = {
    { "debleedEnabled", &DrumSettings::debleedEnabled },
};
} // namespace

void DrumSettings::validate() noexcept
{
    const DrumSettings defaults;
    for (const auto& f : kFloatFields)
        this->*f.member = clampFinite(this->*f.member, f.lo, f.hi, defaults.*f.member);
}

std::string DrumSettings::serialise() const
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

DrumSettings DrumSettings::deserialise(const std::string& text)
{
    DrumSettings s;
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

} // namespace mixcore::drum
