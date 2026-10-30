#pragma once
// Sample kits for drum replacement.
//
// A kit is a list of velocity layers (quiet -> loud), each holding one or more
// round-robin variations. Kits are built on the message thread (from WAV files
// or the built-in starter kits) and handed to the audio thread through
// KitExchange, which never blocks, never allocates on the audio thread, and
// frees old kits on the message thread only once the audio thread can no longer
// be using them.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mixcore::drum {

struct SampleBuffer
{
    std::string name;
    double sampleRate = 48000.0;
    std::vector<float> left;
    std::vector<float> right; ///< Empty for mono samples.
    float peak = 0.0f;        ///< Filled by analyse().
    float loudnessDb = -150.0f; ///< Loudest 10 ms RMS window, dBFS. Filled by analyse().

    size_t frames() const noexcept { return left.size(); }
    bool stereo() const noexcept { return !right.empty() && right.size() == left.size(); }
};

/// Measures peak and loudness (loudest 10 ms RMS window).
void analyse(SampleBuffer& s);

struct SampleLayer
{
    float loudnessDb = -150.0f;          ///< Mean loudness of the layer's samples.
    std::vector<SampleBuffer> roundRobin;
};

struct SampleKit
{
    std::string id;   ///< "starter:snare", "files", ...
    std::string name; ///< Shown to the user.
    std::vector<SampleLayer> layers; ///< Quiet -> loud.

    bool empty() const noexcept { return layers.empty(); }
    int sampleCount() const noexcept;
    /// e.g. "3 velocity layers x 2 round robin (6 samples)".
    std::string describe() const;
};

/// Builds a kit from loose samples: sorts them by loudness and groups samples
/// within `roundRobinToleranceDb` of each other into one layer (as round-robin
/// variations). At most `maxLayers` layers (closest neighbours are merged).
/// Silent or empty samples are dropped.
SampleKit buildKit(std::string id, std::string name, std::vector<SampleBuffer> samples,
                   float roundRobinToleranceDb = 1.5f, int maxLayers = 16);

/// Lock-free hand-over of kits from the message thread to the audio thread.
class KitExchange
{
public:
    KitExchange() = default;
    KitExchange(const KitExchange&) = delete;
    KitExchange& operator=(const KitExchange&) = delete;

    // ---- Message thread ----
    /// Makes `kit` the active kit (nullptr = no kit). The previous kit is
    /// retired and freed later by collectGarbage().
    void publish(std::unique_ptr<SampleKit> kit);
    /// Frees retired kits the audio thread can no longer be using.
    void collectGarbage();
    /// The kit most recently published (message-thread view).
    const SampleKit* published() const noexcept { return owner_.get(); }
    size_t retiredCount() const noexcept { return retired_.size(); }

    // ---- Audio thread ----
    /// Call once at the start of each block.
    const SampleKit* acquire() const noexcept { return active_.load(std::memory_order_seq_cst); }
    /// Call once at the end of each block.
    void blockDone() noexcept { blocks_.fetch_add(1, std::memory_order_seq_cst); }

private:
    std::atomic<const SampleKit*> active_ { nullptr };
    std::atomic<std::uint64_t> blocks_ { 0 };
    std::unique_ptr<SampleKit> owner_;
    std::vector<std::pair<std::unique_ptr<SampleKit>, std::uint64_t>> retired_;
};

} // namespace mixcore::drum
