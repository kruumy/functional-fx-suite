cmake_minimum_required(VERSION 3.25)
project(functional_fx_suite VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# plugins ship without a runtime installer dependency (sanitizer builds override this)
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE)
    # universal binaries; AE 2023+ and current OFX hosts need macOS 12 or later anyway
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0)
    endif()
endif()

option(FFX_BUILD_OFX "Build OFX plugins" ON)
option(FFX_BUILD_AE "Build After Effects plugins (needs the AE SDK)" ON)
option(FFX_BUILD_TESTS "Build fxrender and the regression tests" ON)
set(FFX_AE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/AfterEffectsSDK/Examples" CACHE PATH
    "AE SDK 'Examples' folder (contains Headers, Util, Resources)")

set(FFX_EFFECTS shake tint rays glow ultraglow glowaura glowdark glowrings glint
    hotspots edgerays vignette badcolors duotone tritone edgedetect edgecolorize
    flicker mosaic scanlines halftone halftonecolor jpegdamage pixelsort digitaldamage tvdamage filmdamage
    distort distortblur distortchroma distortrgb warpchroma warpfisheye warppuddle zblur blurmocurves
    pseudocolor psykoblobs psykostripes ascii rollingbar zdefocus lensflare timewarprgb
    timedisplace motiondetect freezeframe jitterframes reverseedits lensdirt digitize
    heatdistortion)

add_library(${PROJECT_NAME} STATIC
    src/core/blur.cpp
    src/core/bokeh.cpp
    src/core/color.cpp
    src/core/edges.cpp
    src/core/fft.cpp
    src/core/flare.cpp
    src/core/glow.cpp
    src/core/mask.cpp
    src/core/mip.cpp
    src/core/retime.cpp
    src/core/rays.cpp
    src/core/warp.cpp
    src/core/zdepth.cpp
    src/effects/ascii.cpp
    src/effects/badcolors.cpp
    src/effects/blurmocurves.cpp
    src/effects/catalog.cpp
    src/effects/digitaldamage.cpp
    src/effects/digitize.cpp
    src/effects/distort.cpp
    src/effects/distortblur.cpp
    src/effects/distortchroma.cpp
    src/effects/distortrgb.cpp
    src/effects/duotone.cpp
    src/effects/edgecolorize.cpp
    src/effects/edgedetect.cpp
    src/effects/edgerays.cpp
    src/effects/filmdamage.cpp
    src/effects/flicker.cpp
    src/effects/freezeframe.cpp
    src/effects/glint.cpp
    src/effects/glow.cpp
    src/effects/glowaura.cpp
    src/effects/glowdark.cpp
    src/effects/glowrings.cpp
    src/effects/halftone.cpp
    src/effects/halftonecolor.cpp
    src/effects/heatdistortion.cpp
    src/effects/hotspots.cpp
    src/effects/jitterframes.cpp
    src/effects/jpegdamage.cpp
    src/effects/lensdirt.cpp
    src/effects/lensflare.cpp
    src/effects/mosaic.cpp
    src/effects/motiondetect.cpp
    src/effects/pixelsort.cpp
    src/effects/pseudocolor.cpp
    src/effects/reverseedits.cpp
    src/effects/psykoblobs.cpp
    src/effects/psykostripes.cpp
    src/effects/rays.cpp
    src/effects/rollingbar.cpp
    src/effects/scanlines.cpp
    src/effects/shake.cpp
    src/effects/timewarprgb.cpp
    src/effects/timedisplace.cpp
    src/effects/tint.cpp
    src/effects/tritone.cpp
    src/effects/tvdamage.cpp
    src/effects/ultraglow.cpp
    src/effects/vignette.cpp
    src/effects/warpchroma.cpp
    src/effects/warpfisheye.cpp
    src/effects/warppuddle.cpp
    src/effects/zblur.cpp
    src/effects/zdefocus.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC src)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC /W4 /permissive- /utf-8 /fp:fast /EHsc)
else()
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra -Wpedantic)
endif()

if(FFX_BUILD_TESTS)
    enable_testing()
    add_executable(fxrender tools/fxrender.cpp)
    target_link_libraries(fxrender PRIVATE ${PROJECT_NAME})
    add_executable(effects_test tests/effects_test.cpp)
    target_link_libraries(effects_test PRIVATE ${PROJECT_NAME})
    add_test(NAME effects_test COMMAND effects_test)
endif()

# ffx_add_host_test(<host> <system include dirs...>) runs tests/<host>_host_test.cpp on every f_<effect>_<host> plugin
function(ffx_add_host_test host)
    set(target ${host}_host_test)
    add_executable(${target} tests/${target}.cpp)
    target_include_directories(${target} SYSTEM PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE ${PROJECT_NAME})
    list(TRANSFORM FFX_EFFECTS PREPEND "$<TARGET_FILE:f_" OUTPUT_VARIABLE files)
    list(TRANSFORM files APPEND "_${host}>")
    add_test(NAME ${target} COMMAND ${target} ${files})
endfunction()

if(FFX_BUILD_AE)
    if(EXISTS "${FFX_AE_SDK_DIR}/Headers/AE_Effect.h")
        include(cmake/ae_plugin.cmake)
        ffx_add_ae_plugin(shake CATEGORY "Functional Distort" TIME_VARYING)
        ffx_add_ae_plugin(tint CATEGORY "Functional Adjust")
        ffx_add_ae_plugin(rays CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(glow CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(ultraglow CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(glowaura CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(glowdark CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(glowrings CATEGORY "Functional Lighting")
        ffx_add_ae_plugin(glint CATEGORY "Functional Lighting")
        ffx_add_ae_plugin(hotspots CATEGORY "Functional Adjust")
        ffx_add_ae_plugin(edgerays CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(vignette CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(badcolors CATEGORY "Functional Adjust")
        ffx_add_ae_plugin(duotone CATEGORY "Functional Adjust")
        ffx_add_ae_plugin(tritone CATEGORY "Functional Adjust")
        ffx_add_ae_plugin(edgedetect CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(edgecolorize CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(flicker CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(mosaic CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(scanlines CATEGORY "Functional Stylize" TIME_VARYING)
        ffx_add_ae_plugin(halftone CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(halftonecolor CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(jpegdamage CATEGORY "Functional Stylize" TIME_VARYING)
        ffx_add_ae_plugin(pixelsort CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(digitaldamage CATEGORY "Functional Stylize" TIME_VARYING)
        ffx_add_ae_plugin(tvdamage CATEGORY "Functional Stylize" TIME_VARYING)
        ffx_add_ae_plugin(filmdamage CATEGORY "Functional Stylize" TIME_VARYING)
        ffx_add_ae_plugin(distort CATEGORY "Functional Distort")
        ffx_add_ae_plugin(distortblur CATEGORY "Functional Distort")
        ffx_add_ae_plugin(distortchroma CATEGORY "Functional Distort")
        ffx_add_ae_plugin(distortrgb CATEGORY "Functional Distort")
        ffx_add_ae_plugin(warpchroma CATEGORY "Functional Distort")
        ffx_add_ae_plugin(warpfisheye CATEGORY "Functional Distort")
        ffx_add_ae_plugin(warppuddle CATEGORY "Functional Distort" TIME_VARYING)
        ffx_add_ae_plugin(zblur CATEGORY "Functional Blur")
        ffx_add_ae_plugin(zdefocus CATEGORY "Functional Blur")
        ffx_add_ae_plugin(blurmocurves CATEGORY "Functional Blur" TIME_VARYING WIDE_TIME)
        ffx_add_ae_plugin(pseudocolor CATEGORY "Functional Adjust")
        ffx_add_ae_plugin(psykoblobs CATEGORY "Functional Render" TIME_VARYING)
        ffx_add_ae_plugin(psykostripes CATEGORY "Functional Render" TIME_VARYING)
        ffx_add_ae_plugin(ascii CATEGORY "Functional Stylize")
        ffx_add_ae_plugin(rollingbar CATEGORY "Functional Stylize" TIME_VARYING)
        ffx_add_ae_plugin(lensflare CATEGORY "Functional Lighting" TIME_VARYING)
        ffx_add_ae_plugin(timewarprgb CATEGORY "Functional Time" WIDE_TIME)
        ffx_add_ae_plugin(timedisplace CATEGORY "Functional Time" WIDE_TIME)
        ffx_add_ae_plugin(motiondetect CATEGORY "Functional Time" WIDE_TIME)
        ffx_add_ae_plugin(freezeframe CATEGORY "Functional Time" WIDE_TIME)
        ffx_add_ae_plugin(jitterframes CATEGORY "Functional Time" WIDE_TIME)
        ffx_add_ae_plugin(reverseedits CATEGORY "Functional Time" WIDE_TIME)
        ffx_add_ae_plugin(lensdirt CATEGORY "Functional Lighting")
        ffx_add_ae_plugin(digitize CATEGORY "Functional Stylize" WIDE_TIME)
        ffx_add_ae_plugin(heatdistortion CATEGORY "Functional Distort" TIME_VARYING)
        if(FFX_BUILD_TESTS AND WIN32)
            ffx_add_host_test(ae "${FFX_AE_SDK_DIR}/Headers" "${FFX_AE_SDK_DIR}/Headers/SP")
            target_compile_definitions(ae_host_test PRIVATE _WINDOWS)
        endif()
    else()
        message(WARNING "AE SDK not found at '${FFX_AE_SDK_DIR}'; skipping After Effects plugins")
    endif()
endif()

# openfx
if(FFX_BUILD_OFX)
    include(FetchContent)
    # headers only: SOURCE_SUBDIR points at a folder without CMakeLists so nothing else is configured
    FetchContent_Declare(openfx
        GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
        GIT_TAG ab779510b2655b4d11a7e01e5c521f9aa8c88976 # OFX_Release_1.5.1
        GIT_SHALLOW FALSE
        SOURCE_SUBDIR include
    )
    FetchContent_MakeAvailable(openfx)
    include(cmake/ofx_plugin.cmake)
    foreach(fx IN LISTS FFX_EFFECTS)
        ffx_add_ofx_plugin(${fx})
    endforeach()
    if(FFX_BUILD_TESTS AND WIN32)
        ffx_add_host_test(ofx "${openfx_SOURCE_DIR}/include")
    endif()
endif()
