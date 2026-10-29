# Gate / expander module: source-aware gating with side-chain filters,
# hysteresis, hold, transient trigger, lookahead and threshold learning.
# No JUCE dependency.

add_library(mixcore_gate STATIC
    src/GateSettings.cpp
    src/GateLearner.cpp
    src/GateProfiles.cpp
    src/NoiseGate.cpp)
add_library(mixcore::gate ALIAS mixcore_gate)
target_include_directories(mixcore_gate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mixcore_gate PUBLIC mixcore::dsp)
mixcore_set_warnings(mixcore_gate)
