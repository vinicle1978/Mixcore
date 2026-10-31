cmake_minimum_required(VERSION 3.22)

# Must be set before project() or CMake ignores it.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(MixCore VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MIXCORE_BUILD_PLUGIN "Build the JUCE plugin and standalone app" ON)
option(MIXCORE_BUILD_TESTS "Build the engine test suite" ON)
option(MIXCORE_WARNINGS_AS_ERRORS "Treat engine warnings as errors" OFF)

set(MIXCORE_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (fetched from GitHub if empty)")
set(MIXCORE_CATCH2_PATH "" CACHE PATH "Path to a local Catch2 v3 checkout (fetched from GitHub if empty)")

include(cmake/CompilerWarnings.cmake)

# --- JUCE-independent engine ---------------------------------------------------
add_subdirectory(engine)
add_subdirectory(modules/gain)
add_subdirectory(modules/gate)
add_subdirectory(modules/drum)
add_subdirectory(modules/eq)
add_subdirectory(modules/strip)

# --- JUCE plugin / standalone ---------------------------------------------------
if(MIXCORE_BUILD_PLUGIN)
    include(FetchContent)
    if(MIXCORE_JUCE_PATH)
        add_subdirectory(${MIXCORE_JUCE_PATH} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
    add_subdirectory(plugins/mixcore)
endif()

# --- Tests ----------------------------------------------------------------------
if(MIXCORE_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    if(MIXCORE_CATCH2_PATH)
        add_subdirectory(${MIXCORE_CATCH2_PATH} ${CMAKE_BINARY_DIR}/catch2 EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.7.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(Catch2)
    endif()
    add_subdirectory(tests)
endif()
