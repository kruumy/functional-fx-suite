if(WIN32)
    set(FFX_OFX_ARCH Win64)
elseif(APPLE)
    set(FFX_OFX_ARCH MacOS)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(FFX_OFX_ARCH Linux-arm64)
else()
    set(FFX_OFX_ARCH Linux-x86-64)
endif()

# ffx_add_ofx_plugin(<effect>) builds ofx/f_<effect>.ofx.bundle
function(ffx_add_ofx_plugin effect)
    set(target f_${effect}_ofx)
    set(gen "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
    set(bundle "${CMAKE_BINARY_DIR}/ofx/f_${effect}.ofx.bundle")
    set(FFX_EFFECT ${effect})
    configure_file("${PROJECT_SOURCE_DIR}/cmake/plugin_effect.cpp.in" "${gen}/plugin_effect.cpp" @ONLY)
    if(APPLE)
        set(FFX_NAME f_${effect})
        set(FFX_BUNDLE_EXECUTABLE f_${effect}.ofx)
        set(FFX_BUNDLE_TYPE BNDL)
        set(FFX_BUNDLE_SIGNATURE ????)
        configure_file("${PROJECT_SOURCE_DIR}/cmake/Info.plist.in" "${bundle}/Contents/Info.plist" @ONLY)
    endif()

    add_library(${target} MODULE
        "${gen}/plugin_effect.cpp"
        src/hosts/ofx/pixels.cpp
        src/hosts/ofx/plugin.cpp
    )
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME f_${effect}
        PREFIX ""
        SUFFIX ".ofx"
        # $<0:> keeps multi-config generators from appending a config folder inside the bundle
        LIBRARY_OUTPUT_DIRECTORY "${bundle}/Contents/${FFX_OFX_ARCH}$<0:>"
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # hosts ship their own C++ runtimes; keep ours private to the plugin
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    target_include_directories(${target} SYSTEM PRIVATE "${openfx_SOURCE_DIR}/include")
    target_compile_definitions(${target} PRIVATE
        FFX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        FFX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    )
    target_link_libraries(${target} PRIVATE ${PROJECT_NAME})
endfunction()
