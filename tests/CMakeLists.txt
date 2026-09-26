cmake_minimum_required(VERSION 3.28)
project(bink32w_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest: prefer a local checkout so an offline machine can build the
# tests without touching the network (the FetchContent download below is the
# only network access in the whole build).
set(BINK_GTEST_SRC "" CACHE PATH "Local googletest source tree (skips the network fetch)")
if(NOT BINK_GTEST_SRC AND EXISTS "${CMAKE_SOURCE_DIR}/third-party/googletest/CMakeLists.txt")
    set(BINK_GTEST_SRC "${CMAKE_SOURCE_DIR}/third-party/googletest")
endif()

# The whole project links the static CRT (see CMAKE_MSVC_RUNTIME_LIBRARY in
# the root CMakeLists), so gtest must be built the same way - ON would compile
# gtest with /MD and the link would fail with LNK2038 RuntimeLibrary mismatch.
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
if(BINK_GTEST_SRC)
    message(STATUS "Using local googletest: ${BINK_GTEST_SRC}")
    add_subdirectory("${BINK_GTEST_SRC}" "${CMAKE_BINARY_DIR}/_deps/googletest-build" EXCLUDE_FROM_ALL)
else()
    message(STATUS "Fetching googletest from GitHub - for offline builds set "
                   "-DBINK_GTEST_SRC=<path> or -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<path>")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    FetchContent_MakeAvailable(googletest)
endif()

enable_testing()

add_executable(bink32w_tests
    mock_waveout.cpp
    test_mix_crc32.cpp
    test_bpp_from_flags.cpp
    test_trim_right.cpp
    test_read_u32_u16.cpp
    test_config_parser.cpp
    test_audio_decoder.cpp
    test_integration.cpp
    test_third_party.cpp
    test_wav_player.cpp
    test_bink_container.cpp
    test_logging.cpp
    test_proxy_core.cpp
    test_corrupt_data.cpp
    test_binkioprocessor.cpp
    test_uncovered.cpp
    test_mix_blowfish.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/mix_crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/logging.cpp
    ${CMAKE_SOURCE_DIR}/src/audio_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/binkw32_proxy.cpp
    wav_player_mock.cpp
)

target_include_directories(bink32w_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(bink32w_tests PRIVATE
    BINK_GROUP_5
    BINK_REAL_DLL="binkw32_1.0q.dll"
    THIRD_PARTY_DIR="${CMAKE_SOURCE_DIR}/third-party"
    BINK_TEST_BUILD
    _CRT_SECURE_NO_WARNINGS
)
if(TARGET binkw32_g5)
    target_compile_definitions(bink32w_tests PRIVATE PROXY_DLL_PATH="$<TARGET_FILE:binkw32_g5>")
elseif(TARGET binkw32_g7)
    target_compile_definitions(bink32w_tests PRIVATE PROXY_DLL_PATH="$<TARGET_FILE:binkw32_g7>")
else()
    message(WARNING
        "tests: neither binkw32_g5 nor binkw32_g7 is in BINK_GROUPS - integration tests "
        "that load the proxy DLL will FAIL (run with BINK_TEST_ALLOW_NO_PROXY=1 to skip them instead).")
endif()
if(MSVC)
    target_compile_options(bink32w_tests PRIVATE /W3 /WX-)
else()
    target_compile_options(bink32w_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
target_link_libraries(bink32w_tests PRIVATE GTest::gtest_main kernel32 user32 winmm)

include(GoogleTest)
gtest_discover_tests(bink32w_tests)
