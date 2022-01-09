cmake_minimum_required(VERSION 3.21)
project(bn_memcpy)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
        binaryninjaapi
        GIT_REPOSITORY https://github.com/Vector35/binaryninja-api
        )
FetchContent_MakeAvailable(binaryninjaapi)

if(WIN32)
    set(OUTPUT_FILE ${PROJECT_NAME}.dll)
else()
    set(OUTPUT_FILE ${PROJECT_NAME}.so)
endif()

add_library(bn_memcpy SHARED "bn_memcpy.cpp")
target_link_libraries(bn_memcpy PRIVATE binaryninjaapi)
add_custom_command(
        TARGET bn_memcpy
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${OUTPUT_FILE} ${BN_PLUGIN_DIR})