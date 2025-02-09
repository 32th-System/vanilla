cmake_minimum_required(VERSION 3.21)

project(Vanilla C CXX)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_C_STANDARD 23)

OPTION(VANILLA_BUILD_DESKTOP "Build Qt app for Linux desktop" ON)
OPTION(VANILLA_BUILD_RPI "Build SDL2 app for Raspberry Pi" OFF)
OPTION(VANILLA_BUILD_TESTS "Build unit tests for Vanilla" OFF)

add_subdirectory(lib)
if (LINUX AND NOT ANDROID)
	add_subdirectory(pipe)
endif()
if (VANILLA_BUILD_DESKTOP)
    add_subdirectory(app)
endif()
if (VANILLA_BUILD_RPI)
    add_subdirectory(rpi)
endif()
