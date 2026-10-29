#include "mixcore/gate/GateProfiles.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace mixcore::gate {

namespace {

GateSettings make(float thr, float range, float ratio, float hyst, float attack, float hold, float release, float hpf,
                  float lpf, bool rms, bool transient, float lookahead)
{
    GateSettings s;
    s.enabled = true;
    s.thresholdDb = thr;
    s.rangeDb = range;
    s.ratio = ratio;
    s.hysteresisDb = hyst;
    s.attackMs = attack;
    s.holdMs = hold;
    s.releaseMs = release;
    s.sidechainHpfHz = hpf;
    s.sidechainLpfHz = lpf;
    s.rmsDetector = rms;
    s.transientTrigger = transient;
    s.lookaheadMs = lookahead;
    s.validate();
    return s;
}

std::vector<GateProfile> build()
{
    //                          thr    range ratio hyst attack hold release  scHPF  scLPF  rms    trans  look
    return {
        { "Vocal - Gentle Expander",
          "Turns the mic down a little between phrases (up to 10 dB) instead of muting it. Breaths, consonants and "
          "reverb tails stay natural. The safe default for lead vocals.",
          make(-50.0f, 10.0f, 2.0f, 6.0f, 5.0f, 200.0f, 400.0f, 120.0f, 10000.0f, true, false, 0.0f) },
        { "Vocal - Tight Gate",
          "Cuts the mic by 20 dB between phrases for loud stages with lots of monitor and drum spill. Long hold so "
          "word endings are not clipped.",
          make(-45.0f, 20.0f, 20.0f, 8.0f, 2.0f, 250.0f, 300.0f, 150.0f, 8000.0f, true, false, 0.0f) },
        { "Backing Vocal",
          "Keeps idle backing-vocal mics from adding spill and room sound while the singer is not singing.",
          make(-45.0f, 12.0f, 2.5f, 6.0f, 5.0f, 250.0f, 500.0f, 150.0f, 8000.0f, true, false, 0.0f) },
        { "Speech",
          "Gentle expansion for lecterns and handheld MCs: lowers room noise and PA spill between sentences.",
          make(-50.0f, 12.0f, 2.0f, 6.0f, 5.0f, 300.0f, 500.0f, 100.0f, 8000.0f, true, false, 0.0f) },
        { "Kick",
          "Fast peak gate keyed on 30-150 Hz so snare and hi-hat bleed do not open it. Transient trigger and 1.5 ms "
          "lookahead keep the beater attack intact.",
          make(-26.0f, 40.0f, 20.0f, 8.0f, 0.1f, 50.0f, 150.0f, 30.0f, 150.0f, false, true, 1.5f) },
        { "Snare",
          "Fast peak gate keyed on 150 Hz-5 kHz so kick and cymbal bleed are ignored. 30 dB range keeps some snare "
          "rattle and room so it does not sound chopped.",
          make(-26.0f, 30.0f, 20.0f, 8.0f, 0.1f, 80.0f, 200.0f, 150.0f, 5000.0f, false, true, 1.5f) },
        { "Toms",
          "Opens on tom hits (60 Hz-1 kHz), ignores cymbals, and releases slowly to keep the tom's natural ring.",
          make(-28.0f, 30.0f, 20.0f, 8.0f, 0.2f, 150.0f, 400.0f, 60.0f, 1000.0f, false, true, 1.5f) },
        { "Overheads",
          "Very gentle expansion (up to 8 dB) to reduce room wash between hits without making cymbals sound gated.",
          make(-40.0f, 8.0f, 1.5f, 6.0f, 1.0f, 100.0f, 500.0f, 200.0f, 20000.0f, false, false, 0.0f) },
        { "Instrument",
          "General-purpose expander for guitar amps, keys and other mics: lowers noise and spill when not playing.",
          make(-50.0f, 15.0f, 3.0f, 6.0f, 2.0f, 150.0f, 300.0f, 60.0f, 12000.0f, true, false, 0.0f) },
    };
}

std::map<std::string, float> toMap(const GateSettings& s)
{
    std::map<std::string, float> m;
    std::istringstream in(s.serialise());
    std::string pair;
    while (std::getline(in, pair, ';'))
    {
        const auto eq = pair.find('=');
        if (eq != std::string::npos)
            m[pair.substr(0, eq)] = std::strtof(pair.c_str() + eq + 1, nullptr);
    }
    return m;
}

} // namespace

const std::vector<GateProfile>& gateProfiles()
{
    static const std::vector<GateProfile> profiles = build();
    return profiles;
}

const GateProfile* findGateProfile(const std::string& name)
{
    for (const auto& p : gateProfiles())
        if (p.name == name)
            return &p;
    return nullptr;
}

bool gateSettingsMatch(const GateSettings& a, const GateSettings& b, float tolerance)
{
    auto ca = a, cb = b;
    ca.validate();
    cb.validate();
    const auto ma = toMap(ca), mb = toMap(cb);
    for (const auto& [key, y] : mb)
    {
        const auto it = ma.find(key);
        if (it == ma.end() || std::abs(it->second - y) > tolerance * std::max(1.0f, std::abs(y)))
            return false;
    }
    return true;
}

} // namespace mixcore::gate
