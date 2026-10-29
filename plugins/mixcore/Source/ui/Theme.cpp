#include "Theme.h"

#include "VineAudioAssets.h"

namespace mixcore::plugin::theme {

juce::Image logoImage()
{
    return juce::ImageCache::getFromMemory(VineAudioAssets::vineaudio_logo_png, VineAudioAssets::vineaudio_logo_pngSize);
}

juce::Image emblemImage()
{
    return juce::ImageCache::getFromMemory(VineAudioAssets::vineaudio_emblem_png, VineAudioAssets::vineaudio_emblem_pngSize);
}

} // namespace mixcore::plugin::theme
