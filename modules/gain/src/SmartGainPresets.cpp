#include "mixcore/gain/SmartGainPresets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace mixcore::gain {

namespace {

// Every preset starts from the validated defaults and changes only what makes
// that source different, so new parameters automatically get sane values.
SmartGainSettings base() { return SmartGainSettings {}; }

std::vector<FactoryPreset> buildPresets()
{
    std::vector<FactoryPreset> p;

    // ---------------------------------------------------------------- Lead vocal
    {
        auto s = base(); // the engine defaults ARE the natural lead vocal profile
        p.push_back({ "Lead Vocal - Natural", "Lead Vocal",
                      "Moderate riding that keeps phrases even without sounding processed. A good first choice for most singers.",
                      s });
    }
    {
        auto s = base();
        s.riderStrength = 0.7f;
        s.deadbandDb = 1.0f;
        s.riderMaxUpDb = 5.0f;
        s.riderMaxDownDb = 7.0f;
        s.riderRangeDb = 7.0f;
        s.riderAttackMs = 100.0f;
        s.riderReleaseMs = 400.0f;
        s.riderMaxRateDbPerSec = 8.0f;
        p.push_back({ "Lead Vocal - Controlled", "Lead Vocal",
                      "Stronger, faster riding for a vocal that must stay on top of a busy band.",
                      s });
    }
    {
        auto s = base();
        s.riderStrength = 0.6f;
        s.riderRangeDb = 9.0f;
        s.riderMaxUpDb = 6.0f;
        s.riderMaxDownDb = 9.0f;
        s.riderMaxRateDbPerSec = 8.0f;
        s.phraseHoldMs = 250.0f;
        s.peakGuardEnabled = true;
        p.push_back({ "Lead Vocal - Dynamic Singer", "Lead Vocal",
                      "Wider range for singers who move between whispers and belting. Peak guard on to catch sudden loud notes (adds 2 ms latency).",
                      s });
    }
    {
        auto s = base();
        s.riderStrength = 0.65f;
        s.deadbandDb = 1.0f;
        s.riderMaxUpDb = 5.0f;
        s.riderMaxDownDb = 7.0f;
        s.riderRangeDb = 7.0f;
        s.peakGuardEnabled = true;
        s.peakGuardCeilingDb = -3.0f;
        p.push_back({ "Lead Vocal - Live Stream", "Lead Vocal",
                      "Consistent level for online listeners on phones and earbuds. Peak guard on for a protected stream feed (adds 2 ms latency, keep it off for in-ear monitor feeds).",
                      s });
    }

    // ----------------------------------------------------------- Backing vocals
    {
        auto s = base();
        s.targetRmsDb = -21.0f;
        s.riderStrength = 0.35f;
        s.deadbandDb = 2.0f;
        s.riderMaxUpDb = 3.0f;
        s.riderMaxDownDb = 4.0f;
        s.riderRangeDb = 4.0f;
        s.riderAttackMs = 200.0f;
        s.riderReleaseMs = 800.0f;
        s.riderMaxRateDbPerSec = 4.0f;
        s.phraseHoldMs = 250.0f;
        p.push_back({ "Backing Vocal - Airy", "Backing Vocal",
                      "Gentle, slow riding that leaves breathy, soft parts natural and sitting behind the lead. Tone (air, brightness) comes from EQ.",
                      s });
    }
    {
        auto s = base();
        s.targetRmsDb = -20.0f;
        s.riderStrength = 0.75f;
        s.deadbandDb = 1.0f;
        s.riderMaxUpDb = 5.0f;
        s.riderMaxDownDb = 6.0f;
        s.riderAttackMs = 80.0f;
        s.riderReleaseMs = 300.0f;
        s.riderMaxRateDbPerSec = 9.0f;
        p.push_back({ "Backing Vocal - Tight", "Backing Vocal",
                      "Firm, quick riding so stacked or harmony parts stay locked at an even level under the lead.",
                      s });
    }
    {
        auto s = base();
        s.targetRmsDb = -22.0f;
        s.riderStrength = 0.4f;
        s.deadbandDb = 2.0f;
        s.riderMaxUpDb = 4.0f;
        s.riderMaxDownDb = 5.0f;
        s.riderRangeDb = 5.0f;
        s.riderAttackMs = 250.0f;
        s.riderReleaseMs = 1000.0f;
        s.riderMaxRateDbPerSec = 3.0f;
        s.phraseHoldMs = 400.0f;
        p.push_back({ "Backing Vocal - Choir / Group", "Backing Vocal",
                      "Very slow, broad levelling for a group or choir mic, so the blend between voices is preserved.",
                      s });
    }

    // ------------------------------------------------------------------ Speech
    {
        auto s = base();
        s.riderStrength = 0.7f;
        s.deadbandDb = 1.0f;
        s.riderMaxUpDb = 6.0f;
        s.riderMaxDownDb = 6.0f;
        s.riderRangeDb = 6.0f;
        s.riderMaxRateDbPerSec = 7.0f;
        s.phraseHoldMs = 300.0f;
        s.idleReleaseDelayMs = 3000.0f;
        s.detectorHpfHz = 80.0f;
        p.push_back({ "Speech - Pastor / Host", "Speech",
                      "Even level for preaching, announcements and MCs who move closer to and further from the mic.",
                      s });
    }
    {
        auto s = base();
        s.riderStrength = 0.75f;
        s.deadbandDb = 1.0f;
        s.riderMaxUpDb = 6.0f;
        s.riderMaxDownDb = 6.0f;
        s.phraseHoldMs = 300.0f;
        s.idleReleaseDelayMs = 3000.0f;
        s.detectorHpfHz = 80.0f;
        s.peakGuardEnabled = true;
        p.push_back({ "Speech - Stream / Podcast", "Speech",
                      "Consistent spoken level for streams and podcasts, with the peak guard on (adds 2 ms latency).",
                      s });
    }

    // ------------------------------------------------------------------- Drums
    // On drums the rider is OFF by default: riding would change the drummer's
    // dynamics. Smart Gain is used here for calibrated static gain, clip
    // detection and peak monitoring. The detector settings are tuned so short,
    // separated hits count as "active" for calibration: a long phrase hold
    // bridges the gaps between hits, and the detector high-pass matches the
    // drum's frequency range.
    {
        auto s = base();
        s.targetRmsDb = -22.0f;
        s.riderEnabled = false;
        s.activityConfirmMs = 10.0f;
        s.phraseHoldMs = 1200.0f;
        s.activityMarginDb = 12.0f;
        s.detectorHpfHz = 30.0f;
        p.push_back({ "Kick Drum", "Drums",
                      "Static gain staging for kick: calibrate while the drummer plays a normal groove. Rider off to preserve dynamics.",
                      s });
    }
    {
        auto s = base();
        s.targetRmsDb = -22.0f;
        s.riderEnabled = false;
        s.activityConfirmMs = 10.0f;
        s.phraseHoldMs = 1200.0f;
        s.activityMarginDb = 12.0f;
        s.detectorHpfHz = 150.0f;
        p.push_back({ "Snare Drum", "Drums",
                      "Static gain staging for snare: calibrate on a normal groove (not just ghost notes or rim shots). Rider off to preserve dynamics.",
                      s });
    }
    {
        auto s = base();
        s.targetRmsDb = -22.0f;
        s.riderEnabled = false;
        s.activityConfirmMs = 10.0f;
        s.phraseHoldMs = 1500.0f;
        s.activityMarginDb = 12.0f;
        s.detectorHpfHz = 60.0f;
        p.push_back({ "Toms", "Drums",
                      "Static gain staging for rack and floor toms: calibrate on a fill. Rider off to preserve dynamics.",
                      s });
    }

    for (auto& preset : p)
        preset.settings.validate();
    return p;
}

} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = buildPresets();
    return presets;
}

const FactoryPreset* findFactoryPreset(const std::string& name)
{
    for (const auto& p : factoryPresets())
        if (p.name == name)
            return &p;
    return nullptr;
}

SmartGainSettings applyPreset(const SmartGainSettings& current, const SmartGainSettings& preset)
{
    SmartGainSettings s = preset;
    s.staticGainDb = current.staticGainDb;
    s.bypass = current.bypass;
    s.validate();
    return s;
}

bool matchesPreset(const SmartGainSettings& current, const SmartGainSettings& preset, float tolerance)
{
    // Compare the serialised key/value lists, so every field (including ones
    // added in future) is covered without maintaining a separate list here.
    auto toMap = [](const SmartGainSettings& settings) {
        std::map<std::string, float> m;
        std::istringstream in(settings.serialise());
        std::string pair;
        while (std::getline(in, pair, ';'))
        {
            const auto eq = pair.find('=');
            if (eq != std::string::npos)
                m[pair.substr(0, eq)] = std::strtof(pair.c_str() + eq + 1, nullptr);
        }
        return m;
    };

    SmartGainSettings cur = current;
    cur.validate();
    const auto a = toMap(cur);
    const auto b = toMap(applyPreset(current, preset));
    for (const auto& [key, y] : b)
    {
        if (key == "version" || key == "staticGainDb" || key == "bypass")
            continue;
        const auto it = a.find(key);
        if (it == a.end())
            return false;
        // Relative tolerance for large values (ms, Hz), absolute for small ones.
        if (std::abs(it->second - y) > tolerance * std::max(1.0f, std::abs(y)))
            return false;
    }
    return true;
}

} // namespace mixcore::gain
