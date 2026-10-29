# Drum module: de-bleed (phase 1); drum replacement planned (phase 2).
# No JUCE dependency.

add_library(mixcore_drum STATIC
    src/DrumSettings.cpp
    src/DrumProfiles.cpp
    src/DeBleed.cpp)
add_library(mixcore::drum ALIAS mixcore_drum)
target_include_directories(mixcore_drum PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mixcore_drum PUBLIC mixcore::dsp)
mixcore_set_warnings(mixcore_drum)
