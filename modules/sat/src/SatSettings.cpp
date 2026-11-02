#include "mixcore/sat/SatSettings.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace mixcore::sat {

const char* satTypeName(SatType t) noexcept
{
    static const char* names[kNumSatTypes] = { "Tape", "Tube", "Console", "Clip" };
    const int i = static_cast<int>(t);
    return names[(i >= 0 && i < kNumSatTypes) ? i : 0];
}

void SatSettings::validate() noexcept
{
    const SatSettings d;
    driveDb = clampFinite(driveDb, 0.0f, 36.0f, d.driveDb);
    mixPercent = clampFinite(mixPercent, 0.0f, 100.0f, 100.0f);
    outputDb = clampFinite(outputDb, -24.0f, 12.0f, 0.0f);
    if (static_cast<int>(type) < 0 || static_cast<int>(type) >= kNumSatTypes)
        type = d.type;
    if (quality != SatQuality::Live && quality != SatQuality::Studio)
        quality = d.quality;
}

std::string SatSettings::serialise() const
{
    std::ostringstream out;
    out.precision(9);
    out << "version=1;enabled=" << (enabled ? 1 : 0) << ";type=" << static_cast<int>(type) << ";drive=" << driveDb
        << ";mix=" << mixPercent << ";output=" << outputDb << ";autoGain=" << (autoGain ? 1 : 0)
        << ";quality=" << static_cast<int>(quality) << ';';
    return out.str();
}

SatSettings SatSettings::deserialise(const std::string& text)
{
    SatSettings s;
    std::istringstream in(text);
    std::string pair;
    while (std::getline(in, pair, ';'))
    {
        const auto eq = pair.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = pair.substr(0, eq), value = pair.substr(eq + 1);
        char* end = nullptr;
        const float v = std::strtof(value.c_str(), &end);
        if (end == value.c_str())
            continue;
        if (key == "enabled") s.enabled = v >= 0.5f;
        else if (key == "type") s.type = static_cast<SatType>(std::clamp(static_cast<int>(std::lround(v)), 0, kNumSatTypes - 1));
        else if (key == "drive") s.driveDb = v;
        else if (key == "mix") s.mixPercent = v;
        else if (key == "output") s.outputDb = v;
        else if (key == "autoGain") s.autoGain = v >= 0.5f;
        else if (key == "quality") s.quality = v >= 0.5f ? SatQuality::Studio : SatQuality::Live;
    }
    s.validate();
    return s;
}

bool approximatelyEqual(const SatSettings& a, const SatSettings& b, float tolerance) noexcept
{
    const auto near = [tolerance](float x, float y) { return std::abs(x - y) <= tolerance * std::max(1.0f, std::abs(y)); };
    return a.enabled == b.enabled && a.type == b.type && near(a.driveDb, b.driveDb) && near(a.mixPercent, b.mixPercent)
           && near(a.outputDb, b.outputDb) && a.autoGain == b.autoGain && a.quality == b.quality;
}

} // namespace mixcore::sat
