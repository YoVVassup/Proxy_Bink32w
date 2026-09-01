cmake_minimum_required(VERSION 3.28)
project(bink32w LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# CMakeLists.txt — Build configuration for Proxy_Bink32w
#
# Builds proxy binkw32.dll for selected Bink version groups.
# Each group produces a separate DLL output in GROUP_N/Release/ with:
#   - binkw32.dll        (proxy DLL)
#   - binkw32_X.Yz.dll   (real Bink DLL, copied from Real/)
#   - binkw32.cfg        (audio config, copied from project root)
#
# BINK_GROUPS: semicolon-separated list of ordinal groups (or "all").
# Default: 5;7 (RA2/RA2YR default + best video quality)
#
# Usage:
#   cmake -B build -DBINK_GROUPS="5;7"               # RA2 + best quality
#   cmake -B build -DBINK_GROUPS="all"                # all 19 groups
#   cmake -B build -DBINK_GROUPS="7;10;15;18"         # specific groups
#
# Run tools/generate_ordinals.ps1 to regenerate ordinal tables from DLLs.
# ============================================================================

set(BINK_GROUPS "5;7" CACHE STRING "Semicolon-separated list of ordinal groups to build (or 'all')")
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set(GROUP_VERSIONS_1  "1.8c")
set(GROUP_DLL_1  "binkw32_1.8c.dll")
set(GROUP_DLLS_1  "binkw32_1.8c.dll;binkw32_1.8d.dll;binkw32_1.8f.dll;binkw32_1.8g.dll;binkw32_1.8i.dll;binkw32_1.8j.dll;binkw32_1.8k.dll;binkw32_1.8l.dll;binkw32_1.8s.dll;binkw32_1.8v.dll;binkw32_1.8w.dll;binkw32_1.8x.dll")
set(GROUP_INFO_1  "Bink 1.8x (12 versions: 1.8c-1.8x). Early DX9, BinkControlBackgroundIO.")

set(GROUP_VERSIONS_2  "1.5e")
set(GROUP_DLL_2  "binkw32_1.5e.dll")
set(GROUP_DLLS_2  "binkw32_1.5e.dll;binkw32_1.5g.dll;binkw32_1.5j.dll;binkw32_1.5k.dll;binkw32_1.5L.dll;binkw32_1.5p.dll;binkw32_1.5r.dll;binkw32_1.5s.dll;binkw32_1.5u.dll;binkw32_1.5v.dll")
set(GROUP_INFO_2  "Bink 1.5x-1.6 (10 versions: 1.5e-1.5v). BinkCopyToBufferRect, BinkDX8SurfaceType.")

set(GROUP_VERSIONS_3  "1.5x")
set(GROUP_DLL_3  "binkw32_1.5x.dll")
set(GROUP_DLLS_3  "binkw32_1.5x.dll;binkw32_1.5y.dll;binkw32_1.6a.dll;binkw32_1.6b.dll;binkw32_1.6c.dll;binkw32_1.6d.dll;binkw32_1.6e.dll;binkw32_1.6g.dll;binkw32_1.7b.dll")
set(GROUP_INFO_3  "Bink 1.5x-1.7 (9 versions: 1.5x-1.7b). BinkSetMemory, YUV blits.")

set(GROUP_VERSIONS_4  "1.9a")
set(GROUP_DLL_4  "binkw32_1.9a.dll")
set(GROUP_DLLS_4  "binkw32_1.9a.dll;binkw32_1.9c.dll;binkw32_1.9e.dll;binkw32_1.9g.dll;binkw32_1.9h.dll")
set(GROUP_INFO_4  "Bink 1.9x early (5 versions: 1.9a-1.9h). BinkDoFrameAsync, BinkShouldSkip.")

set(GROUP_VERSIONS_5  "1.0n;1.0p;1.0q;1.0s;1.0t")
set(GROUP_DLL_5  "binkw32_1.0q.dll")
set(GROUP_DLLS_5  "binkw32_1.0n.dll;binkw32_1.0p.dll;binkw32_1.0q.dll;binkw32_1.0s.dll;binkw32_1.0t.dll")
set(GROUP_INFO_5  "Bink 1.0q (5 versions: 1.0n-1.0t). *** RA2/RA2YR default ***. 83 ordinals, ExpandBink, RADSetMemory.")

set(GROUP_VERSIONS_6  "1.9i")
set(GROUP_DLL_6  "binkw32_1.9i.dll")
set(GROUP_DLLS_6  "binkw32_1.9i.dll;binkw32_1.9L.dll;binkw32_1.9m.dll;binkw32_1.9n.dll;binkw32_1.9p.dll")
set(GROUP_INFO_6  "Bink 1.9x mid (5 versions: 1.9i-1.9p). BinkDoFramePlane, BinkSetMemory.")

set(GROUP_VERSIONS_7  "1.9q;1.9r;1.9u")
set(GROUP_DLL_7  "binkw32_1.9u.dll")
set(GROUP_DLLS_7  "binkw32_1.9q.dll;binkw32_1.9r.dll;binkw32_1.9u.dll")
set(GROUP_INFO_7  "Bink 1.9u (3 versions: 1.9q-1.9u). *** Best video quality ***. 73 ordinals, BinkSetMemory.")

set(GROUP_VERSIONS_8  "1.0v")
set(GROUP_DLL_8  "binkw32_1.0v.dll")
set(GROUP_DLLS_8  "binkw32_1.0v.dll;binkw32_1.0w.dll;binkw32_1.0x.dll")
set(GROUP_INFO_8  "Bink 1.0x late (3 versions: 1.0v-1.0x). RADSetMemory, no ExpandBink.")

set(GROUP_VERSIONS_9  "1.8a")
set(GROUP_DLL_9  "binkw32_1.8a.dll")
set(GROUP_DLLS_9  "binkw32_1.8a.dll;binkw32_1.8b.dll")
set(GROUP_INFO_9  "Bink 1.8a-1.8b (2 versions). BinkControlBackgroundIO, BinkShouldSkip.")

set(GROUP_VERSIONS_10 "1.2i")
set(GROUP_DLL_10  "binkw32_1.2i.dll")
set(GROUP_DLLS_10  "binkw32_1.2i.dll;binkw32_1.5a.dll")
set(GROUP_INFO_10 "Bink 1.2i-1.5a (2 versions). BinkDX8SurfaceType, BinkSetMemory.")

set(GROUP_VERSIONS_11 "1.1b")
set(GROUP_DLL_11  "binkw32_1.1b.dll")
set(GROUP_DLLS_11  "binkw32_1.1b.dll;binkw32_1.2a.dll")
set(GROUP_INFO_11 "Bink 1.1x-1.2a (2 versions: 1.1b, 1.2a). BinkDX8SurfaceType, RADSetMemory.")

set(GROUP_VERSIONS_12 "1.2c")
set(GROUP_DLL_12  "binkw32_1.2c.dll")
set(GROUP_DLLS_12  "binkw32_1.2c.dll;binkw32_1.2d.dll")
set(GROUP_INFO_12 "Bink 1.2c-1.2d (2 versions). BinkSetMixBins.")

set(GROUP_VERSIONS_13 "1.1c")
set(GROUP_DLL_13  "binkw32_1.1c.dll")
set(GROUP_DLLS_13  "binkw32_1.1c.dll")
set(GROUP_INFO_13 "Bink 1.1c (1 version). BinkDX8SurfaceType, RADTimerRead.")

set(GROUP_VERSIONS_14 "1.0k")
set(GROUP_DLL_14  "binkw32_1.0k.dll")
set(GROUP_DLLS_14  "binkw32_1.0k.dll")
set(GROUP_INFO_14 "Bink 1.0k (1 version). No BinkSetIO. ExpandBink + ExpandBundleSizes.")

set(GROUP_VERSIONS_15 "1.0m")
set(GROUP_DLL_15  "binkw32_1.0m.dll")
set(GROUP_DLLS_15  "binkw32_1.0m.dll")
set(GROUP_INFO_15 "Bink 1.0m (1 version). ExpandBink + ExpandBundleSizes, no RADSetMemory.")

set(GROUP_VERSIONS_16 "1.0h")
set(GROUP_DLL_16  "binkw32_1.0h.dll")
set(GROUP_DLLS_16  "binkw32_1.0h.dll")
set(GROUP_INFO_16 "Bink 1.0h (1 version). YUV_blit (generic) + YUV_blit_mask. ExpandBink.")

set(GROUP_VERSIONS_17 "1.0i")
set(GROUP_DLL_17  "binkw32_1.0i.dll")
set(GROUP_DLLS_17  "binkw32_1.0i.dll")
set(GROUP_INFO_17 "Bink 1.0i (1 version). YUV_blit (generic) + YUV_blit_mask. ExpandBink, RADTimerRead.")

set(GROUP_VERSIONS_18 "1.7d")
set(GROUP_DLL_18  "binkw32_1.7d.dll")
set(GROUP_DLLS_18  "binkw32_1.7d.dll")
set(GROUP_INFO_18 "Bink 1.7d (1 version). BinkDX9SurfaceType, BinkRegisterFrameBuffers, 86 ordinals.")

set(GROUP_VERSIONS_19 "1.0j")
set(GROUP_DLL_19  "binkw32_1.0j.dll")
set(GROUP_DLLS_19  "binkw32_1.0j.dll")
set(GROUP_INFO_19 "Bink 1.0j (1 version). No ExpandBink, only ExpandBundleSizes.")

function(add_bink_proxy TARGET_NAME BINK_GROUP_NUM REAL_DLL_NAME REAL_DLLS DEF_FILE)
    add_library(${TARGET_NAME} SHARED
        src/binkw32_proxy.cpp src/logging.cpp src/config.cpp
        src/audio_decoder.cpp src/wav_player.cpp
        ${DEF_FILE} src/version_info.rc
    )
    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "binkw32" MODULE_PREFIX "" VERSION "2.0.3.0" SOVERSION "2"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/GROUP_${BINK_GROUP_NUM}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/GROUP_${BINK_GROUP_NUM}"
    )
    # Convert semicolon list to comma-separated for C macro
    string(REPLACE ";" "," _compat_csv "${REAL_DLLS}")
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W3 /WX- /DBINK_GROUP_${BINK_GROUP_NUM} /DBINK_REAL_DLL="${REAL_DLL_NAME}" /DBINK_COMPAT_DLLS="${_compat_csv}")
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -DBINK_GROUP_${BINK_GROUP_NUM} -DBINK_REAL_DLL="${REAL_DLL_NAME}" -DBINK_COMPAT_DLLS="${_compat_csv}")
    endif()
    target_link_libraries(${TARGET_NAME} PRIVATE kernel32 user32 winmm)

    foreach(_dll_name ${REAL_DLLS})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/Real/${_dll_name}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_dll_name}"
            COMMENT "Copying ${_dll_name} to GROUP_${BINK_GROUP_NUM}"
        )
    endforeach()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/binkw32.cfg"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/binkw32.cfg"
        COMMENT "Copying binkw32.cfg to GROUP_${BINK_GROUP_NUM}"
    )
endfunction()

if(BINK_GROUPS STREQUAL "all")
    set(_groups 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19)
else()
    set(_groups ${BINK_GROUPS})
endif()

foreach(GROUP_NUM ${_groups})
    set(_dll "${GROUP_DLL_${GROUP_NUM}}")
    set(_dlls "${GROUP_DLLS_${GROUP_NUM}}")
    set(_info "${GROUP_INFO_${GROUP_NUM}}")
    message(STATUS "  Group ${GROUP_NUM}: ${_info}")
    add_bink_proxy("binkw32_g${GROUP_NUM}" ${GROUP_NUM} "${_dll}" "${_dlls}" src/exports.def)
endforeach()

if(BUILD_TESTS)
    # Test player — standalone tool for integration testing
    add_executable(test_bink_player tools/test_bink_player.cpp)
    target_link_libraries(test_bink_player PRIVATE kernel32 user32 gdi32 winmm shell32)
    set_target_properties(test_bink_player PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    )

    # Minimal Bink test — minimal test for quick validation
    add_executable(test_bink_minimal tools/test_bink_minimal.cpp)
    target_link_libraries(test_bink_minimal PRIVATE kernel32 user32)
    set_target_properties(test_bink_minimal PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    )
endif()
