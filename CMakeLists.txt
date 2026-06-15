cmake_minimum_required(VERSION 3.28)
project(bink32w LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(add_bink_proxy TARGET_NAME BINK_DEFINE)
    add_library(${TARGET_NAME} SHARED
        src/binkw32_proxy.cpp
        src/exports.def
        src/version_info.rc
    )
    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "binkw32"
        MODULE_PREFIX ""
        VERSION "1.0.0.0"
        SOVERSION "1"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${BINK_DEFINE}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${BINK_DEFINE}"
    )
    target_compile_options(${TARGET_NAME} PRIVATE /W3 /WX- /D${BINK_DEFINE})
    target_link_libraries(${TARGET_NAME} PRIVATE kernel32 user32)
endfunction()

add_bink_proxy(binkw32_10q BINK_10Q)
add_bink_proxy(binkw32_19u BINK_19U)
