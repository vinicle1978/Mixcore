# Core utilities (header-only) and DSP primitives. No JUCE dependency.

add_library(mixcore_core INTERFACE)
add_library(mixcore::core ALIAS mixcore_core)
target_include_directories(mixcore_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
target_compile_features(mixcore_core INTERFACE cxx_std_20)

add_library(mixcore_dsp STATIC
    dsp/src/TruePeakEstimator.cpp
    dsp/src/LookaheadPeakGuard.cpp)
add_library(mixcore::dsp ALIAS mixcore_dsp)
target_include_directories(mixcore_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp/include)
target_link_libraries(mixcore_dsp PUBLIC mixcore::core)
mixcore_set_warnings(mixcore_dsp)
