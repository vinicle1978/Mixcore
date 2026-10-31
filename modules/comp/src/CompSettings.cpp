#include "mixcore/comp/CompSettings.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace mixcore::comp {

void CompSettings::validate() noexcept
{
    const CompSettings d;
    thresholdDb = clampFinite(thresholdDb, -60.0f, 0.0f, d.thresholdDb);
    ratio = clampFinite(ratio, 1.0f, 20.0f, d.ratio);
    kneeDb = clampFinite(kneeDb, 0.0f, 24.0f, d.kneeDb);
    attackMs = clampFinite(attackMs, 0.1f, 100.0f, d.attackMs);
    releaseMs = clampFinite(releaseMs, 10.0f, 2000.0f, d.releaseMs);
    makeupDb = clampFinite(makeupDb, -12.0f, 24.0f, 0.0f);
    mixPercent = clampFinite(mixPercent, 0.0f, 100.0f, 100.0f);
    scHpfHz = clampFinite(scHpfHz, 20.0f, 500.0f, d.scHpfHz);
    if (detector != Detector::Peak && detector != Detector::Rms)
        detector = d.detector;
}

std::string CompSettings::serialise() const
{
    std::ostringstream out;
    out.precision(9);
    out << "version=1;enabled=" << (enabled ? 1 : 0) << ";threshold=" << thresholdDb << ";ratio=" << ratio << ";knee=" << kneeDb
        << ";attack=" << attackMs << ";release=" << releaseMs << ";autoRelease=" << (autoRelease ? 1 : 0) << ";makeup=" << makeupDb
        << ";autoMakeup=" << (autoMakeup ? 1 : 0) << ";mix=" << mixPercent << ";scHpfOn=" << (scHpfOn ? 1 : 0)
        << ";scHpf=" << scHpfHz << ";detector=" << static_cast<int>(detector) << ';';
    return out.str();
}

CompSettings CompSettings::deserialise(const std::string& text)
{
    CompSettings s;
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
        else if (key == "threshold") s.thresholdDb = v;
        else if (key == "ratio") s.ratio = v;
        else if (key == "knee") s.kneeDb = v;
        else if (key == "attack") s.attackMs = v;
        else if (key == "release") s.releaseMs = v;
        else if (key == "autoRelease") s.autoRelease = v >= 0.5f;
        else if (key == "makeup") s.makeupDb = v;
        else if (key == "autoMakeup") s.autoMakeup = v >= 0.5f;
        else if (key == "mix") s.mixPercent = v;
        else if (key == "scHpfOn") s.scHpfOn = v >= 0.5f;
        else if (key == "scHpf") s.scHpfHz = v;
        else if (key == "detector") s.detector = v >= 0.5f ? Detector::Rms : Detector::Peak;
    }
    s.validate();
    return s;
}

bool approximatelyEqual(const CompSettings& a, const CompSettings& b, float tolerance) noexcept
{
    const auto near = [tolerance](float x, float y) { return std::abs(x - y) <= tolerance * std::max(1.0f, std::abs(y)); };
    return a.enabled == b.enabled && near(a.thresholdDb, b.thresholdDb) && near(a.ratio, b.ratio) && near(a.kneeDb, b.kneeDb)
           && near(a.attackMs, b.attackMs) && near(a.releaseMs, b.releaseMs) && a.autoRelease == b.autoRelease
           && near(a.makeupDb, b.makeupDb) && a.autoMakeup == b.autoMakeup && near(a.mixPercent, b.mixPercent)
           && a.scHpfOn == b.scHpfOn && near(a.scHpfHz, b.scHpfHz) && a.detector == b.detector;
}

float gainChangeDb(float x, float t, float r, float w) noexcept
{
    // Soft-knee static curve (Giannoulis, Massberg & Reiss, JAES 2012).
    const float over = x - t;
    const float slope = 1.0f / r - 1.0f; // <= 0
    if (2.0f * over <= -w)
        return 0.0f;
    if (w > 0.0f && 2.0f * std::abs(over) <= w)
    {
        const float k = over + 0.5f * w;
        return slope * k * k / (2.0f * w);
    }
    return slope * over;
}

} // namespace mixcore::comp
