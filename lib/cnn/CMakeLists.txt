cmake_minimum_required(VERSION 3.21)

project(cnn_lib LANGUAGES CXX)

file(GLOB_RECURSE CNN_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_library(cnn_lib STATIC ${CNN_SOURCES})

target_include_directories(cnn_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cnn_lib PUBLIC cxx_std_17)

# Reminder flags (currently disabled)
# -O3
# -march=native
# -flto
# -funroll-loops
# -ffast-math
# -fopenmp
#
# Example (disabled):
# target_compile_options(cnn_lib PRIVATE -O3 -funroll-loops -ffast-math -fopenmp)
# target_link_options(cnn_lib PRIVATE -flto)
