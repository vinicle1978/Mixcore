# Warning flags for MixCore's own targets (not applied to JUCE or Catch2).
function(mixcore_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(MIXCORE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
                                                 -Wold-style-cast -Wcast-align -Woverloaded-virtual
                                                 -Wdouble-promotion -Wimplicit-fallthrough)
        if(MIXCORE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
