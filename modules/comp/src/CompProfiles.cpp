#include "mixcore/comp/CompProfiles.h"

namespace mixcore::comp {

namespace {
CompProfile make(std::string name, std::string desc, float t, float r, float knee, float att, float rel, bool autoRel, bool autoMk,
                 float makeup, bool hpfOn, float hpf, Detector d, float mix = 100.0f)
{
    CompProfile p;
    p.name = std::move(name);
    p.description = std::move(desc);
    auto& s = p.settings;
    s.enabled = true;
    s.thresholdDb = t;
    s.ratio = r;
    s.kneeDb = knee;
    s.attackMs = att;
    s.releaseMs = rel;
    s.autoRelease = autoRel;
    s.autoMakeup = autoMk;
    s.makeupDb = makeup;
    s.scHpfOn = hpfOn;
    s.scHpfHz = hpf;
    s.detector = d;
    s.mixPercent = mix;
    s.validate();
    return p;
}

std::vector<CompProfile> build()
{
    using D = Detector;
    std::vector<CompProfile> v;
    v.push_back(make("Lead Vocal", "3:1, soft knee, auto release: evens out phrases without squashing.", -22, 3, 6, 8, 120, true, true, 0,
                     true, 100, D::Rms));
    v.push_back(make("Backing Vocal", "4:1, a bit firmer so the blend stays steady behind the lead.", -24, 4, 6, 10, 150, true, true, 0,
                     true, 120, D::Rms));
    v.push_back(make("Speech", "3.5:1, slower release: steady, intelligible speech.", -24, 3.5f, 8, 10, 200, true, true, 0, true,
                     120, D::Rms));
    v.push_back(make("Kick", "4:1, peak detection, 20 ms attack keeps the beater click.", -14, 4, 3, 20, 80, false, false, 2, false,
                     20, D::Peak));
    v.push_back(make("Snare", "4:1, peak detection, 10 ms attack: controls the body, keeps the crack.", -14, 4, 3, 10, 100, false,
                     false, 2, true, 80, D::Peak));
    v.push_back(make("Toms", "3:1, peak detection, longer release so the tail stays natural.", -16, 3, 4, 15, 150, false, false, 2, true,
                     60, D::Peak));
    v.push_back(make("Instrument", "2.5:1, gentle and slow: steadier level, natural dynamics.", -20, 2.5f, 8, 15, 200, true, true, 0,
                     true, 60, D::Rms));
    v.push_back(make("Parallel Punch", "Heavy 8:1 compression blended at 40 % under the dry signal: density without losing transients.",
                     -28, 8, 3, 5, 100, false, true, 0, true, 80, D::Peak, 40.0f));
    return v;
}
} // namespace

const std::vector<CompProfile>& compProfiles()
{
    static const std::vector<CompProfile> profiles = build();
    return profiles;
}

const CompProfile* findCompProfile(const std::string& name)
{
    for (const auto& p : compProfiles())
        if (p.name == name)
            return &p;
    return nullptr;
}

} // namespace mixcore::comp
