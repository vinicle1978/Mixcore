# Compressor module: zero-latency feed-forward compressor with soft knee,
# program-dependent (auto) release, auto makeup, parallel mix, sidechain
# high-pass, Comp Coach (listen & suggest) and source profiles. No JUCE.

add_library(mixcore_comp STATIC
    src/CompSettings.cpp
    src/Compressor.cpp
    src/CompCoach.cpp
    src/CompProfiles.cpp)
add_library(mixcore::comp ALIAS mixcore_comp)
target_include_directories(mixcore_comp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mixcore_comp PUBLIC mixcore::dsp mixcore::gain)
mixcore_set_warnings(mixcore_comp)
