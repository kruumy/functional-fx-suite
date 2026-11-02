if(WIN32)
    enable_language(RC)
endif()

# literals shared by the PiPL and the C++ static_asserts in src/hosts/ae/plugin.cpp
set(FFX_AE_OUTFLAGS_STATIC 0x6000000) # DEEP_COLOR_AWARE | SEND_UPDATE_PARAMS_UI
set(FFX_AE_OUTFLAG_TIME 0x4)          # NON_PARAM_VARY
set(FFX_AE_OUTFLAG_WIDE_TIME 0x2)     # WIDE_TIME_INPUT
set(FFX_AE_OUTFLAGS2_STATIC 0x8001408) # SMART_RENDER | FLOAT_COLOR_AWARE | THREADED_RENDERING | GROUP_START_COLLAPSED
set(FFX_AE_OUTFLAG2_WIDE_TIME 0x20000) # AUTOMATIC_WIDE_TIME_INPUT
math(EXPR FFX_AE_VERSION
    "(${PROJECT_VERSION_MAJOR} << 19) | (${PROJECT_VERSION_MINOR} << 15) | (${PROJECT_VERSION_PATCH} << 11) | 1")

# ffx_add_ae_plugin(<effect> CATEGORY <name> [TIME_VARYING] [WIDE_TIME])
# builds ae/f_<effect>.aex on Windows and ae/f_<effect>.plugin on macOS
# WIDE_TIME: the effect reads params or frames at other times, so AE tracks those for its cache
function(ffx_add_ae_plugin effect)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "TIME_VARYING;WIDE_TIME" "CATEGORY" "")
    if(NOT WIN32 AND NOT APPLE)
        message(STATUS "After Effects has no Linux version; skipping f_${effect} for AE")
        return()
    endif()

    set(target f_${effect}_ae)
    set(gen "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
    set(FFX_EFFECT ${effect})
    set(FFX_NAME f_${effect})
    set(FFX_CATEGORY "${ARG_CATEGORY}")
    set(FFX_AE_OUTFLAGS ${FFX_AE_OUTFLAGS_STATIC})
    set(FFX_AE_OUTFLAGS2 ${FFX_AE_OUTFLAGS2_STATIC})
    set(time_varying 0)
    set(wide_time 0)
    if(ARG_TIME_VARYING)
        math(EXPR FFX_AE_OUTFLAGS "${FFX_AE_OUTFLAGS} | ${FFX_AE_OUTFLAG_TIME}" OUTPUT_FORMAT HEXADECIMAL)
        set(time_varying 1)
    endif()
    if(ARG_WIDE_TIME)
        math(EXPR FFX_AE_OUTFLAGS "${FFX_AE_OUTFLAGS} | ${FFX_AE_OUTFLAG_WIDE_TIME}" OUTPUT_FORMAT HEXADECIMAL)
        math(EXPR FFX_AE_OUTFLAGS2 "${FFX_AE_OUTFLAGS2} | ${FFX_AE_OUTFLAG2_WIDE_TIME}" OUTPUT_FORMAT HEXADECIMAL)
        set(wide_time 1)
    endif()
    configure_file("${PROJECT_SOURCE_DIR}/cmake/plugin_effect.cpp.in" "${gen}/plugin_effect.cpp" @ONLY)
    configure_file("${PROJECT_SOURCE_DIR}/src/hosts/ae/pipl.r.in" "${gen}/pipl.r" @ONLY)

    if(WIN32)
        # PiPL -> .rc through the SDK's PiPLtool, linked in as a Windows resource
        set(resource "${gen}/pipl.rc")
        add_custom_command(
            OUTPUT "${resource}"
            COMMAND ${CMAKE_COMMAND} "-DCOMPILER=${CMAKE_CXX_COMPILER}" "-DSDK=${FFX_AE_SDK_DIR}"
                    "-DINPUT=${gen}/pipl.r" "-DOUTPUT=${resource}" -P "${PROJECT_SOURCE_DIR}/cmake/pipl.cmake"
            DEPENDS "${gen}/pipl.r" "${PROJECT_SOURCE_DIR}/cmake/pipl.cmake"
            COMMENT "Compiling PiPL for f_${effect}"
            VERBATIM
        )
    else()
        # PiPL -> .rsrc through Rez, shipped in the bundle's Resources folder
        set(resource "${gen}/f_${effect}.rsrc")
        add_custom_command(
            OUTPUT "${resource}"
            COMMAND xcrun Rez -o "${resource}" -useDF -define __MACH__ -arch x86_64 -arch arm64
                    -i "${FFX_AE_SDK_DIR}/Headers" -i "${FFX_AE_SDK_DIR}/Headers/SP" -i "${FFX_AE_SDK_DIR}/Resources"
                    "${gen}/pipl.r"
            DEPENDS "${gen}/pipl.r"
            COMMENT "Compiling PiPL for f_${effect}"
            VERBATIM
        )
        set_source_files_properties("${resource}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        set(FFX_BUNDLE_EXECUTABLE f_${effect})
        set(FFX_BUNDLE_TYPE eFKT)
        set(FFX_BUNDLE_SIGNATURE FXTC)
        configure_file("${PROJECT_SOURCE_DIR}/cmake/Info.plist.in" "${gen}/Info.plist" @ONLY)
    endif()

    add_library(${target} MODULE
        "${gen}/plugin_effect.cpp"
        "${resource}"
        src/hosts/ae/pixels.cpp
        src/hosts/ae/plugin.cpp
    )
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME f_${effect}
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ae$<0:>"
    )
    if(WIN32)
        set_target_properties(${target} PROPERTIES SUFFIX ".aex")
        # the SDK headers key their Windows typedefs off this, not _WIN32
        target_compile_definitions(${target} PRIVATE _WINDOWS)
    else()
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION plugin
            MACOSX_BUNDLE_INFO_PLIST "${gen}/Info.plist"
        )
    endif()
    target_include_directories(${target} SYSTEM PRIVATE
        "${FFX_AE_SDK_DIR}/Headers"
        "${FFX_AE_SDK_DIR}/Headers/SP"
        "${FFX_AE_SDK_DIR}/Util"
    )
    target_compile_definitions(${target} PRIVATE
        FFX_AE_OUTFLAGS=${FFX_AE_OUTFLAGS}
        FFX_AE_OUTFLAGS2=${FFX_AE_OUTFLAGS2}
        FFX_AE_VERSION=${FFX_AE_VERSION}
        FFX_AE_TIME_VARYING=${time_varying}
        FFX_AE_WIDE_TIME=${wide_time}
        FFX_AE_MATCH_NAME="f_${effect}"
        FFX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        FFX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        FFX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    )
    target_link_libraries(${target} PRIVATE ${PROJECT_NAME})
endfunction()
