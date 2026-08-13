cmake_minimum_required(VERSION 3.20)
project(forgechain VERSION 0.1.0 LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()


option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_ASAN "Enable AddressSanitizer + UBSan" OFF)
option(ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)


add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(project_warnings INTERFACE -Wno-array-bounds)
endif()
if(ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(project_warnings INTERFACE -Werror)
endif()

if(ENABLE_ASAN)
    add_library(project_sanitizers INTERFACE)
    target_compile_options(project_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(project_sanitizers INTERFACE -fsanitize=address,undefined)
endif()


find_package(OpenSSL REQUIRED)



add_subdirectory(src/app)
add_subdirectory(src/core)
add_subdirectory(src/crypto)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
