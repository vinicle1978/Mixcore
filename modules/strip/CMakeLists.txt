# Channel strip: Smart Gain -> De-bleed -> Gate -> Replace -> EQ -> Compressor, plus channel presets. No JUCE dependency.

add_library(mixcore_strip STATIC
    src/ChannelPresets.cpp
    src/StripCoach.cpp)
add_library(mixcore::strip ALIAS mixcore_strip)
target_include_directories(mixcore_strip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mixcore_strip PUBLIC mixcore::gain mixcore::gate mixcore::drum mixcore::eq mixcore::comp)
mixcore_set_warnings(mixcore_strip)
