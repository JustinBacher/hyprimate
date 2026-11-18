# Development-only wrapper project. It adds the root project unchanged as a subdirectory and layers the diagnostic
# targets (benchmarks) on top, so the production CMakeLists.txt, its flags and its targets never learn about tooling.
# Configure it through `tools/dev.py`, which supplies the isolated build directory and instrumentation flags.
cmake_minimum_required(VERSION 3.25)
project(hyprimate-dev LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

enable_testing()
if(NOT HYPRIMATE_SOURCE_DIR)
    get_filename_component(HYPRIMATE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
endif()
# HYPRIMATE_SOURCE_DIR may point at another checkout (a baseline worktree) so both sides of a comparison are
# built by the same wrapper.
add_subdirectory(${HYPRIMATE_SOURCE_DIR} hyprimate)

add_subdirectory(bench)
