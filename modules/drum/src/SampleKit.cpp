#include "mixcore/drum/SampleKit.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mixcore::drum {

void analyse(SampleBuffer& s)
{
    s.peak = 0.0f;
    const size_t n = s.frames();
    const bool st = s.stereo();
    for (size_t i = 0; i < n; ++i)
    {
        s.peak = std::max(s.peak, std::abs(s.left[i]));
        if (st)
            s.peak = std::max(s.peak, std::abs(s.right[i]));
    }
    const auto win = static_cast<size_t>(std::max(1.0, 0.010 * s.sampleRate));
    double best = 0.0, acc = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        double v = static_cast<double>(s.left[i]);
        if (st)
            v = 0.5 * (v + static_cast<double>(s.right[i]));
        acc += v * v;
        if (i >= win)
        {
            double o = static_cast<double>(s.left[i - win]);
            if (st)
                o = 0.5 * (o + static_cast<double>(s.right[i - win]));
            acc -= o * o;
        }
        if (i + 1 >= std::min(win, n))
            best = std::max(best, acc / static_cast<double>(std::min(win, i + 1)));
    }
    // Same convention as the rest of MixCore: a full-scale sine reads -3 dBFS.
    s.loudnessDb = powerToDb(std::max(best, 0.0));
}

int SampleKit::sampleCount() const noexcept
{
    int n = 0;
    for (const auto& l : layers)
        n += static_cast<int>(l.roundRobin.size());
    return n;
}

std::string SampleKit::describe() const
{
    if (layers.empty())
        return "no samples";
    size_t minRr = layers.front().roundRobin.size(), maxRr = minRr;
    for (const auto& l : layers)
    {
        minRr = std::min(minRr, l.roundRobin.size());
        maxRr = std::max(maxRr, l.roundRobin.size());
    }
    std::ostringstream o;
    o << layers.size() << (layers.size() == 1 ? " velocity layer" : " velocity layers");
    if (maxRr > 1)
    {
        o << " x ";
        if (minRr == maxRr)
            o << maxRr;
        else
            o << minRr << "-" << maxRr;
        o << " round robin";
    }
    o << " (" << sampleCount() << (sampleCount() == 1 ? " sample)" : " samples)");
    return o.str();
}

SampleKit buildKit(std::string id, std::string name, std::vector<SampleBuffer> samples, float roundRobinToleranceDb,
                   int maxLayers)
{
    SampleKit kit;
    kit.id = std::move(id);
    kit.name = std::move(name);

    std::vector<SampleBuffer> usable;
    for (auto& s : samples)
    {
        if (s.frames() < 2 || !(s.sampleRate > 0.0))
            continue;
        analyse(s);
        if (s.peak < 1.0e-5f) // silent (below -100 dBFS)
            continue;
        usable.push_back(std::move(s));
    }
    std::stable_sort(usable.begin(), usable.end(),
                     [](const SampleBuffer& a, const SampleBuffer& b) { return a.loudnessDb < b.loudnessDb; });

    for (auto& s : usable)
    {
        if (kit.layers.empty() || s.loudnessDb - kit.layers.back().roundRobin.front().loudnessDb > roundRobinToleranceDb)
            kit.layers.emplace_back();
        kit.layers.back().roundRobin.push_back(std::move(s));
    }

    const auto mean = [](const SampleLayer& l) {
        double sum = 0.0;
        for (const auto& s : l.roundRobin)
            sum += static_cast<double>(s.loudnessDb);
        return static_cast<float>(sum / static_cast<double>(std::max<size_t>(1, l.roundRobin.size())));
    };
    for (auto& l : kit.layers)
        l.loudnessDb = mean(l);

    maxLayers = std::max(1, maxLayers);
    while (static_cast<int>(kit.layers.size()) > maxLayers)
    {
        size_t best = 0;
        float bestGap = 1.0e9f;
        for (size_t i = 0; i + 1 < kit.layers.size(); ++i)
        {
            const float gap = kit.layers[i + 1].loudnessDb - kit.layers[i].loudnessDb;
            if (gap < bestGap)
            {
                bestGap = gap;
                best = i;
            }
        }
        auto& a = kit.layers[best];
        auto& b = kit.layers[best + 1];
        for (auto& s : b.roundRobin)
            a.roundRobin.push_back(std::move(s));
        a.loudnessDb = mean(a);
        kit.layers.erase(kit.layers.begin() + static_cast<std::ptrdiff_t>(best + 1));
    }
    return kit;
}

void KitExchange::publish(std::unique_ptr<SampleKit> kit)
{
    const SampleKit* raw = kit.get();
    auto old = std::move(owner_);
    owner_ = std::move(kit);
    active_.store(raw, std::memory_order_seq_cst);
    if (old)
        retired_.emplace_back(std::move(old), blocks_.load(std::memory_order_seq_cst));
    collectGarbage();
}

void KitExchange::collectGarbage()
{
    // A block in flight when a kit was retired ends at stamp + 1; every block
    // that started after that has already seen the new pointer. So once two
    // more blocks have completed, the retired kit is no longer in use.
    const auto now = blocks_.load(std::memory_order_seq_cst);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [now](const auto& r) { return now >= r.second + 2; }),
                   retired_.end());
}

} // namespace mixcore::drum
