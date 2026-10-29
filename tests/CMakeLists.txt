include(Catch)

set(MIXCORE_TEST_SUPPORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/support)

# Unit, integration and regression tests.
add_executable(mixcore_tests
    dsp/test_primitives.cpp
    dsp/test_smart_gain.cpp
    dsp/test_presets.cpp
    dsp/test_gate.cpp
    dsp/test_debleed.cpp
    regression/test_regression.cpp)
target_link_libraries(mixcore_tests PRIVATE mixcore::gain mixcore::gate mixcore::drum mixcore::strip Catch2::Catch2WithMain)
target_compile_definitions(mixcore_tests PRIVATE
    MIXCORE_REGRESSION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/regression/reference")
mixcore_set_warnings(mixcore_tests)
catch_discover_tests(mixcore_tests TEST_PREFIX "unit:" PROPERTIES LABELS "unit")

# Realtime-safety tests (replaces global new/delete; keep in its own binary).
add_executable(mixcore_realtime_tests
    realtime/test_realtime.cpp
    support/AllocationTracker.cpp)
target_link_libraries(mixcore_realtime_tests PRIVATE mixcore::gain mixcore::strip Catch2::Catch2WithMain)
find_package(Threads REQUIRED)
target_link_libraries(mixcore_realtime_tests PRIVATE Threads::Threads)
mixcore_set_warnings(mixcore_realtime_tests)
catch_discover_tests(mixcore_realtime_tests TEST_PREFIX "realtime:" PROPERTIES LABELS "realtime" RUN_SERIAL TRUE)

# Long-duration performance test.
add_executable(mixcore_performance_tests performance/test_performance.cpp)
target_link_libraries(mixcore_performance_tests PRIVATE mixcore::gain Catch2::Catch2WithMain)
mixcore_set_warnings(mixcore_performance_tests)
catch_discover_tests(mixcore_performance_tests TEST_PREFIX "perf:" PROPERTIES LABELS "performance" RUN_SERIAL TRUE)

# Plugin adapter tests (only when the JUCE plugin is built).
if(TARGET MixCore)
    add_executable(mixcore_plugin_tests plugin/test_plugin.cpp)
    target_link_libraries(mixcore_plugin_tests PRIVATE
        MixCore
        mixcore::strip
        juce::juce_audio_utils
        juce::juce_gui_extra
        Catch2::Catch2WithMain)
    target_include_directories(mixcore_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR}/plugins/mixcore/Source)
    # GUI tests need a display; on headless Linux CI run under xvfb-run.
    catch_discover_tests(mixcore_plugin_tests TEST_PREFIX "plugin:" PROPERTIES LABELS "plugin" DISCOVERY_MODE PRE_TEST)
endif()
