cmake_minimum_required(VERSION 3.25)
project(IncrementalProbe LANGUAGES CXX)

set(PROBE_MODE 3 CACHE STRING "Value compiled into the probe")
set(input "${CMAKE_CURRENT_SOURCE_DIR}/generated.in")
add_custom_command(OUTPUT generated.hpp
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${input}" generated.hpp
    DEPENDS "${input}"
    VERBATIM)
add_executable(probe main.cpp generated.hpp)
target_include_directories(probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(probe PRIVATE PROBE_MODE=${PROBE_MODE})