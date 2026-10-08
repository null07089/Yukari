cmake_minimum_required(VERSION 3.22.1)
project(Yukari)

add_library(yukari SHARED
    main.cpp
    config.cpp
    service_match.cpp
    service_cache.cpp
    binder_hook.cpp
    resource_hook.cpp
    feature_match.cpp
    logger.cpp
)

target_include_directories(yukari PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(yukari PRIVATE
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
)

target_link_libraries(yukari
    log
    dl
)

# Keep only the Zygisk entry point in .dynsym and remove the regular ELF
# symbol table from release artifacts.  This prevents helper names and C++
# implementation symbols from being trivially enumerated with readelf.
target_link_options(yukari PRIVATE
    "-Wl,--gc-sections"
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
)

if (CMAKE_STRIP)
    add_custom_command(TARGET yukari POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:yukari>
        COMMENT "Stripping private Yukari symbols"
    )
endif()

set_target_properties(yukari PROPERTIES
    OUTPUT_NAME "yukari"
)
