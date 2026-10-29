# MixCore JUCE plugin + standalone app. The DSP lives in mixcore::gain; this
# target is only the host/UI adapter.

set(MIXCORE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MIXCORE_FORMATS AU)
endif()

juce_add_plugin(MixCore
    COMPANY_NAME "MixCore Audio"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.mixcoreaudio.mixcore"
    PLUGIN_MANUFACTURER_CODE Mxca
    PLUGIN_CODE Mxcr
    PRODUCT_NAME "MixCore"
    VERSION ${PROJECT_VERSION}
    FORMATS ${MIXCORE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "MixCore processes live microphone input in standalone mode.")

target_sources(MixCore PRIVATE
    Source/Parameters.cpp
    Source/PresetManager.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/GatePanel.cpp)

target_include_directories(MixCore PRIVATE Source)

target_compile_definitions(MixCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MixCore
    PRIVATE
        mixcore::strip
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
