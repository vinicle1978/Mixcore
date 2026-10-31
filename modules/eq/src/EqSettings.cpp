#include "mixcore/eq/EqSettings.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace mixcore::eq {

BandType EqSettings::type(int band) noexcept
{
    switch (band)
    {
        case 0: return BandType::HighPass;
        case 1: return BandType::LowShelf;
        case 5: return BandType::HighShelf;
        case 6: return BandType::LowPass;
        default: return BandType::Bell;
    }
}

const char* EqSettings::bandName(int band) noexcept
{
    static const char* names[kNumBands] = { "High-pass", "Low shelf", "Bell 1", "Bell 2", "Bell 3", "High shelf", "Low-pass" };
    return names[std::clamp(band, 0, kNumBands - 1)];
}

std::array<EqBand, kNumBands> EqSettings::defaultBands() noexcept
{
    std::array<EqBand, kNumBands> b {};
    b[0] = { false, 80.0f, 0.0f, 0.707f, 24.0f };
    b[1] = { true, 100.0f, 0.0f, 0.707f, 24.0f };
    b[2] = { true, 250.0f, 0.0f, 1.0f, 24.0f };
    b[3] = { true, 1000.0f, 0.0f, 1.0f, 24.0f };
    b[4] = { true, 4000.0f, 0.0f, 1.0f, 24.0f };
    b[5] = { true, 8000.0f, 0.0f, 0.707f, 24.0f };
    b[6] = { false, 18000.0f, 0.0f, 0.707f, 12.0f };
    return b;
}

void EqSettings::validate() noexcept
{
    const EqSettings defaults;
    outputDb = clampFinite(outputDb, -18.0f, 18.0f, 0.0f);
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& b = bands[static_cast<size_t>(i)];
        const auto& d = defaults.bands[static_cast<size_t>(i)];
        b.freqHz = clampFinite(b.freqHz, 20.0f, 20000.0f, d.freqHz);
        b.gainDb = clampFinite(b.gainDb, -18.0f, 18.0f, 0.0f);
        b.q = clampFinite(b.q, 0.1f, 10.0f, d.q);
        const auto t = type(i);
        if (t == BandType::LowShelf || t == BandType::HighShelf)
            b.q = std::min(b.q, 2.0f);
        const float s = clampFinite(b.slope, 12.0f, 48.0f, d.slope);
        b.slope = s < 18.0f ? 12.0f : (s < 36.0f ? 24.0f : 48.0f); // snap to 12 / 24 / 48
    }
}

int EqSettings::activeBandCount() const noexcept
{
    int n = 0;
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = bands[static_cast<size_t>(i)];
        const auto t = type(i);
        if (!b.on)
            continue;
        if (t == BandType::HighPass || t == BandType::LowPass || std::abs(b.gainDb) >= 0.05f)
            ++n;
    }
    return n;
}

namespace {
const char* kFieldNames[] = { "on", "f", "g", "q", "s" };
} // namespace

std::string EqSettings::serialise() const
{
    std::ostringstream out;
    out.precision(9);
    out << "version=1;enabled=" << (enabled ? 1 : 0) << ";outputDb=" << outputDb << ';';
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = bands[static_cast<size_t>(i)];
        out << 'b' << i << "on=" << (b.on ? 1 : 0) << ';' << 'b' << i << "f=" << b.freqHz << ';' << 'b' << i << "g=" << b.gainDb
            << ';' << 'b' << i << "q=" << b.q << ';' << 'b' << i << "s=" << b.slope << ';';
    }
    return out.str();
}

EqSettings EqSettings::deserialise(const std::string& text)
{
    EqSettings s;
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
        const float v = std::strtof(value.c_str(), &end);
        if (end == value.c_str())
            continue;
        if (key == "enabled")
            s.enabled = v >= 0.5f;
        else if (key == "outputDb")
            s.outputDb = v;
        else if (key.size() >= 3 && key[0] == 'b' && key[1] >= '0' && key[1] < '0' + kNumBands)
        {
            auto& b = s.bands[static_cast<size_t>(key[1] - '0')];
            const std::string field = key.substr(2);
            if (field == kFieldNames[0])
                b.on = v >= 0.5f;
            else if (field == kFieldNames[1])
                b.freqHz = v;
            else if (field == kFieldNames[2])
                b.gainDb = v;
            else if (field == kFieldNames[3])
                b.q = v;
            else if (field == kFieldNames[4])
                b.slope = v;
        }
    }
    s.validate();
    return s;
}

bool approximatelyEqual(const EqSettings& a, const EqSettings& b, float tolerance) noexcept
{
    const auto near = [tolerance](float x, float y) { return std::abs(x - y) <= tolerance * std::max(1.0f, std::abs(y)); };
    if (a.enabled != b.enabled || !near(a.outputDb, b.outputDb))
        return false;
    for (size_t i = 0; i < static_cast<size_t>(kNumBands); ++i)
    {
        const auto &x = a.bands[i], &y = b.bands[i];
        if (x.on != y.on || !near(x.freqHz, y.freqHz) || !near(x.gainDb, y.gainDb) || !near(x.q, y.q) || !near(x.slope, y.slope))
            return false;
    }
    return true;
}

} // namespace mixcore::eq
