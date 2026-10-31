#include "mixcore/eq/EqProfiles.h"

namespace mixcore::eq {

namespace {

struct B
{
    int band;
    bool on;
    float f, g, q, slope;
};

EqProfile make(std::string name, std::string desc, std::initializer_list<B> changes)
{
    EqProfile p;
    p.name = std::move(name);
    p.description = std::move(desc);
    p.settings.enabled = true;
    for (const auto& c : changes)
        p.settings.bands[static_cast<size_t>(c.band)] = { c.on, c.f, c.g, c.q, c.slope };
    p.settings.validate();
    return p;
}

std::vector<EqProfile> build()
{
    std::vector<EqProfile> v;
    v.push_back(make("Flat", "All bands at 0 dB, filters off.", {}));
    v.push_back(make("Lead Vocal", "High-pass 90 Hz, a little less mud, a touch of presence and air.",
                     { { 0, true, 90, 0, 0.707f, 24 }, { 2, true, 300, -2, 1.0f, 24 }, { 4, true, 3000, 1.5f, 1.0f, 24 },
                       { 5, true, 10000, 2, 0.707f, 24 } }));
    v.push_back(make("Backing Vocal", "Higher high-pass so it sits behind the lead; less low-mid, gentle air.",
                     { { 0, true, 140, 0, 0.707f, 24 }, { 2, true, 250, -3, 1.0f, 24 }, { 5, true, 8000, 1.5f, 0.707f, 24 } }));
    v.push_back(make("Speech", "High-pass 100 Hz, clearer consonants for intelligibility.",
                     { { 0, true, 100, 0, 0.707f, 24 }, { 2, true, 250, -2, 1.0f, 24 }, { 4, true, 3000, 2, 1.0f, 24 } }));
    v.push_back(make("Kick", "Sub weight, less box at 400 Hz, beater click at 3.5 kHz.",
                     { { 0, true, 30, 0, 0.707f, 12 }, { 1, true, 60, 2, 0.707f, 24 }, { 2, true, 400, -4, 1.4f, 24 },
                       { 4, true, 3500, 3, 1.2f, 24 } }));
    v.push_back(make("Snare", "High-pass 90 Hz, less ring around 400 Hz, crack and snap up top.",
                     { { 0, true, 90, 0, 0.707f, 24 }, { 2, true, 400, -2, 1.2f, 24 }, { 4, true, 5000, 2, 1.0f, 24 },
                       { 5, true, 10000, 2, 0.707f, 24 } }));
    v.push_back(make("Toms", "High-pass 60 Hz, less box, a little attack.",
                     { { 0, true, 60, 0, 0.707f, 24 }, { 2, true, 400, -3, 1.2f, 24 }, { 4, true, 4000, 2, 1.0f, 24 } }));
    v.push_back(make("Instrument", "Gentle high-pass at 50 Hz to clear rumble; everything else flat.",
                     { { 0, true, 50, 0, 0.707f, 24 } }));
    return v;
}

} // namespace

const std::vector<EqProfile>& eqProfiles()
{
    static const std::vector<EqProfile> profiles = build();
    return profiles;
}

const EqProfile* findEqProfile(const std::string& name)
{
    for (const auto& p : eqProfiles())
        if (p.name == name)
            return &p;
    return nullptr;
}

} // namespace mixcore::eq
