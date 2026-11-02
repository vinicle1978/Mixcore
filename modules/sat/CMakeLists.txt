# Saturation module: four saturation curves, Live (zero latency) and Studio
# (4x oversampling) quality, auto gain, parallel mix, harmonics analysis,
# Sat Coach (listen & suggest) and source profiles. No JUCE.

add_library(mixcore_sat STATIC
    src/SatSettings.cpp
    src/SatCurves.cpp
    src/Oversampler.cpp
    src/Saturator.cpp
    src/SatCoach.cpp
    src/SatProfiles.cpp)
add_library(mixcore::sat ALIAS mixcore_sat)
target_include_directories(mixcore_sat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mixcore_sat PUBLIC mixcore::dsp mixcore::gain mixcore::comp)
mixcore_set_warnings(mixcore_sat)
