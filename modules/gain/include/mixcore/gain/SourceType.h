#pragma once
// What kind of source a channel carries. Used for wording (Mix Coach, status
// text) and by presets; it does not change any DSP by itself.

#include <string>

namespace mixcore::gain {

enum class SourceType
{
    Vocal = 0,
    Speech = 1,
    Drum = 2,
    Instrument = 3
};

inline constexpr int kNumSourceTypes = 4;

struct SourceWords
{
    const char* noun;      ///< "vocal", "voice", "drum", "instrument"
    const char* name;      ///< "Vocal", "Speech", "Drum", "Instrument" (menus, files)
    const char* gaps;      ///< "between phrases", "between hits", ...
    const char* perform;   ///< "Sing", "Speak", "Play"
    const char* performing;///< "singing", "speaking", "playing"
    const char* tail;      ///< "PHRASE END", "DECAY", "NOTE END"
};

inline const SourceWords& words(SourceType s) noexcept
{
    static const SourceWords table[kNumSourceTypes] = {
        { "vocal", "Vocal", "between phrases", "Sing", "singing", "PHRASE END" },
        { "voice", "Speech", "between sentences", "Speak", "speaking", "PHRASE END" },
        { "drum", "Drum", "between hits", "Play", "playing", "DECAY" },
        { "instrument", "Instrument", "between notes", "Play", "playing", "NOTE END" },
    };
    const int i = static_cast<int>(s);
    return table[(i >= 0 && i < kNumSourceTypes) ? i : 0];
}

inline SourceType sourceFromName(const std::string& n) noexcept
{
    for (int i = 0; i < kNumSourceTypes; ++i)
        if (n == words(static_cast<SourceType>(i)).name)
            return static_cast<SourceType>(i);
    return SourceType::Vocal;
}

inline SourceType sourceFromIndex(int i) noexcept
{
    return (i >= 0 && i < kNumSourceTypes) ? static_cast<SourceType>(i) : SourceType::Vocal;
}

} // namespace mixcore::gain
