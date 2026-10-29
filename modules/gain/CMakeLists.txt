# Smart Gain Stage module: static gain, adaptive rider, peak safety, telemetry,
# calibration and Mix Coach rules. No JUCE dependency.

add_library(mixcore_gain STATIC
    src/SmartGainSettings.cpp
    src/SmartGainStage.cpp
    src/StaticCalibrator.cpp
    src/SmartGainCoach.cpp
    src/SmartGainPresets.cpp)
add_library(mixcore::gain ALIAS mixcore_gain)
target_include_directories(mixcore_gain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mixcore_gain PUBLIC mixcore::dsp)
mixcore_set_warnings(mixcore_gain)
