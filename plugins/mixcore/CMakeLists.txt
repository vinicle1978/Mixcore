# MixCore by VineAudio: JUCE plugin + standalone app. The DSP lives in mixcore::gain; this
# target is only the host/UI adapter.

set(MIXCORE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MIXCORE_FORMATS AU)
endif()

juce_add_plugin(MixCore
    COMPANY_NAME "VineAudio"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.vineaudio.mixcore"
    PLUGIN_MANUFACTURER_CODE Vnau
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
    MICROPHONE_PERMISSION_TEXT "VineAudio MixCore processes live microphone input in standalone mode.")

target_sources(MixCore PRIVATE
    Source/Parameters.cpp
    Source/PresetManager.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/GatePanel.cpp
    Source/ui/DrumPanel.cpp
    Source/ui/ReplacePanel.cpp
    Source/ui/EqPanel.cpp
    Source/ui/Theme.cpp)

target_include_directories(MixCore PRIVATE Source)

# Build identifier shown in the editor header (git commit, or "local").
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_VARIABLE MIXCORE_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET RESULT_VARIABLE MIXCORE_GIT_RESULT)
if(NOT MIXCORE_GIT_RESULT EQUAL 0 OR MIXCORE_GIT_HASH STREQUAL "")
    set(MIXCORE_GIT_HASH "local")
endif()
target_compile_definitions(MixCore PRIVATE MIXCORE_BUILD_ID="${MIXCORE_GIT_HASH}")

# Brand assets (VineAudio logo), compiled into the binary.
juce_add_binary_data(MixCoreAssets
    HEADER_NAME VineAudioAssets.h
    NAMESPACE VineAudioAssets
    SOURCES
        Resources/vineaudio_logo.png
        Resources/vineaudio_emblem.png)
set_target_properties(MixCoreAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(MixCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MixCore
    PRIVATE
        mixcore::strip
        MixCoreAssets
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
