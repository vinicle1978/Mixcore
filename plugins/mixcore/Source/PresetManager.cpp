#include "PresetManager.h"

namespace mixcore::plugin {

namespace {
constexpr const char* kRootTag = "MixCorePreset";
constexpr const char* kModule = "SmartGain";
constexpr int kFormatVersion = 4;
} // namespace

PresetManager::PresetManager() : PresetManager(defaultDirectory())
{
    importLegacyPresets(legacyDirectory(), directory_);
}

PresetManager::PresetManager(juce::File directory) : directory_(std::move(directory)) {}

namespace {
juce::File userDataBase()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base;
}
} // namespace

juce::File PresetManager::defaultDirectory()
{
    return userDataBase().getChildFile("VineAudio").getChildFile("MixCore").getChildFile("Presets");
}

juce::File PresetManager::legacyDirectory()
{
    return userDataBase().getChildFile("MixCore").getChildFile("Presets").getChildFile("Smart Gain");
}

int PresetManager::importLegacyPresets(const juce::File& legacy, const juce::File& destination)
{
    if (!legacy.isDirectory())
        return 0;
    int copied = 0;
    for (const auto& f : legacy.findChildFiles(juce::File::findFiles, false, juce::String("*") + kExtension))
    {
        const auto target = destination.getChildFile(f.getFileName());
        if (target.exists())
            continue; // never overwrite a preset in the new location
        if (destination.createDirectory().wasOk() && f.copyFileTo(target))
            ++copied;
    }
    return copied; // originals are left where they were
}

juce::String PresetManager::sanitiseName(const juce::String& name)
{
    return name.trim().replaceCharacters("\r\n\t", "   ").substring(0, 64).trim();
}

juce::File PresetManager::fileFor(const juce::String& name) const
{
    return directory_.getChildFile(juce::File::createLegalFileName(sanitiseName(name)) + kExtension);
}

std::optional<UserPreset> PresetManager::read(const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(kRootTag) || xml->getStringAttribute("module") != kModule)
        return std::nullopt;

    UserPreset p;
    p.name = xml->getStringAttribute("name", file.getFileNameWithoutExtension());
    // v4: module power and source type (older files: Smart Gain on, vocal).
    p.settings.gainOn = xml->getBoolAttribute("smartGainOn", true);
    p.settings.source = gain::sourceFromName(xml->getStringAttribute("source", "Vocal").toStdString());
    p.kit = xml->getStringAttribute("kit");
    p.file = file;
    if (const auto* s = xml->getChildByName("Settings"))
        p.settings.gain = gain::SmartGainSettings::deserialise(s->getAllSubText().trim().toStdString());
    else
        return std::nullopt;
    if (const auto* g = xml->getChildByName("Gate"))
        p.settings.gate = gate::GateSettings::deserialise(g->getAllSubText().trim().toStdString());
    else
        p.settings.gate = gate::GateSettings {}; // v1 file: gate off
    if (const auto* d = xml->getChildByName("Drum"))
        p.settings.drum = drum::DrumSettings::deserialise(d->getAllSubText().trim().toStdString());
    else
        p.settings.drum = drum::DrumSettings {}; // v1/v2 file: de-bleed off
    return p; // deserialise() already validated every value
}

juce::Array<UserPreset> PresetManager::userPresets() const
{
    juce::Array<UserPreset> result;
    for (const auto& f : directory_.findChildFiles(juce::File::findFiles, false, juce::String("*") + kExtension))
        if (auto p = read(f))
            result.add(*p);

    std::sort(result.begin(), result.end(),
              [](const UserPreset& a, const UserPreset& b) { return a.name.compareNatural(b.name) < 0; });
    return result;
}

std::optional<UserPreset> PresetManager::load(const juce::String& name) const
{
    const auto f = fileFor(name);
    return f.existsAsFile() ? read(f) : std::nullopt;
}

bool PresetManager::exists(const juce::String& name) const { return fileFor(name).existsAsFile(); }

juce::String PresetManager::save(const juce::String& name, const strip::ChannelSettings& settings, const juce::String& kit) const
{
    const auto clean = sanitiseName(name);
    if (clean.isEmpty())
        return "Please enter a preset name.";

    if (const auto made = directory_.createDirectory(); made.failed())
        return "Could not create the presets folder: " + made.getErrorMessage();

    juce::XmlElement root(kRootTag);
    root.setAttribute("version", kFormatVersion);
    root.setAttribute("module", kModule);
    root.setAttribute("name", clean);
    root.setAttribute("source", gain::words(settings.source).name);
    root.setAttribute("smartGainOn", settings.gainOn);
    if (kit.isNotEmpty())
        root.setAttribute("kit", kit);
    root.setAttribute("created", juce::Time::getCurrentTime().toISO8601(true));
    auto* s = root.createNewChildElement("Settings");
    s->addTextElement(juce::String(settings.gain.serialise()));
    auto* g = root.createNewChildElement("Gate");
    g->addTextElement(juce::String(settings.gate.serialise()));
    auto* d = root.createNewChildElement("Drum");
    d->addTextElement(juce::String(settings.drum.serialise()));

    // Write to a temporary file then move it into place, so a crash never
    // leaves a half-written preset.
    const auto target = fileFor(clean);
    juce::TemporaryFile temp(target);
    if (!root.writeTo(temp.getFile()))
        return "Could not write the preset file.";
    if (!temp.overwriteTargetFileWithTemporary())
        return "Could not save the preset file.";
    return {};
}

bool PresetManager::remove(const juce::String& name) const
{
    const auto f = fileFor(name);
    return f.existsAsFile() && f.deleteFile();
}

} // namespace mixcore::plugin
