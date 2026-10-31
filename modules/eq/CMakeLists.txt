# Parametric EQ module: 7-band zero-latency EQ, spectrum analysis, EQ Coach
# (listen & suggest) and source profiles. No JUCE dependency.

add_library(mixcore_eq STATIC
    src/EqSettings.cpp
    src/ParametricEq.cpp
    src/Spectrum.cpp
    src/EqCoach.cpp
    src/EqProfiles.cpp)
add_library(mixcore::eq ALIAS mixcore_eq)
target_include_directories(mixcore_eq PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mixcore_eq PUBLIC mixcore::dsp mixcore::gain)
mixcore_set_warnings(mixcore_eq)
