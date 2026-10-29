#pragma once
// User channel presets (Smart Gain + Gate), stored as small XML files:
//
//   macOS:   ~/Library/Application Support/MixCore/Presets/Smart Gain/<name>.mixpreset
//   Windows: %APPDATA%\MixCore\Presets\Smart Gain\<name>.mixpreset
//   Linux:   ~/.config/MixCore/Presets/Smart Gain/<name>.mixpreset
//
// The same files are shared by every DAW and the standalone app, and can be
// copied between computers. All calls run on the message thread.
//
// File format v2 adds a <Gate> element. v1 files (Smart Gain only, written
// before the gate existed) load with the gate switched off. The folder keeps
// its original name so presets saved with earlier versions still appear.

#include "mixcore/strip/ChannelStrip.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace mixcore::plugin {

struct UserPreset
{
    juce::String name;
    juce::File file;
    strip::ChannelSettings settings;
};

class PresetManager
{
public:
    static constexpr const char* kExtension = ".mixpreset";

    /// Uses the standard per-user location.
    PresetManager();
    /// Uses `directory` (tests, or a custom location).
    explicit PresetManager(juce::File directory);

    static juce::File defaultDirectory();
    const juce::File& directory() const noexcept { return directory_; }

    /// All readable user presets, sorted by name. Unreadable files are skipped.
    juce::Array<UserPreset> userPresets() const;

    std::optional<UserPreset> load(const juce::String& name) const;
    bool exists(const juce::String& name) const;

    /// Writes (or overwrites) a preset. Returns an error message, or empty on success.
    juce::String save(const juce::String& name, const strip::ChannelSettings& settings) const;

    bool remove(const juce::String& name) const;

    /// Trimmed, length-limited name; empty if nothing usable remains.
    static juce::String sanitiseName(const juce::String& name);

private:
    juce::File fileFor(const juce::String& name) const;
    static std::optional<UserPreset> read(const juce::File& file);

    juce::File directory_;
};

} // namespace mixcore::plugin
