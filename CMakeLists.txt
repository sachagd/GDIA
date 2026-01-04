cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GDIA VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})


set(DNNL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DNNL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DNNL_CPU_RUNTIME SEQ CACHE STRING "" FORCE)
set(DNNL_GPU_RUNTIME NONE CACHE STRING "" FORCE)
set(DNNL_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)

add_subdirectory(external/onednn)

add_subdirectory(cnn)
target_link_libraries(${PROJECT_NAME} cnn_lib)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
