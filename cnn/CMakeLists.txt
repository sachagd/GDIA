cmake_minimum_required(VERSION 3.21)

project(cnn_lib LANGUAGES CXX)

file(GLOB CNN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_library(cnn_lib STATIC ${CNN_SOURCES})

target_include_directories(cnn_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(cnn_lib PUBLIC cxx_std_23)

target_link_libraries(cnn_lib PRIVATE dnnl)

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
