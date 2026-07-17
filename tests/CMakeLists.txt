cmake_minimum_required(VERSION 3.28)
project(bink32w_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

set(TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
file(MAKE_DIRECTORY ${TEST_DATA_DIR})

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
    ${CMAKE_SOURCE_DIR}/src/config.cpp
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
    TEST_DATA_DIR="${TEST_DATA_DIR}"
    THIRD_PARTY_DIR="${CMAKE_SOURCE_DIR}/third-party"
    BINK_TEST_BUILD
    _CRT_SECURE_NO_WARNINGS
)
if(MSVC)
    target_compile_options(bink32w_tests PRIVATE /W3 /WX-)
else()
    target_compile_options(bink32w_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
target_link_libraries(bink32w_tests PRIVATE GTest::gtest_main kernel32 user32 winmm)

include(GoogleTest)
gtest_discover_tests(bink32w_tests)
