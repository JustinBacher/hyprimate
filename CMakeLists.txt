cmake_minimum_required(VERSION 3.25)
project(liquid-glass VERSION 0.1.0 LANGUAGES CXX)

# The plugin runs inside the compositor's frame loop: an unoptimised build halves the frame rate during paper
# crumples. Default to the same build type hyprpm uses.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CONFIG_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/config/settings.json)
set(CONFIG_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_config.py)
set(CONFIG_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/config_registration.inc ${CONFIG_GENERATED_DIR}/config_constants.inc
    COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA}
            --header ${CONFIG_GENERATED_DIR}/config_registration.inc
            --constants ${CONFIG_GENERATED_DIR}/config_constants.inc
    DEPENDS ${CONFIG_SCHEMA} ${CONFIG_GENERATOR}
    VERBATIM
)
add_custom_target(generate-config DEPENDS ${CONFIG_GENERATED_DIR}/config_registration.inc ${CONFIG_GENERATED_DIR}/config_constants.inc)
add_custom_target(update-config-docs
    COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA} --readme ${CMAKE_CURRENT_SOURCE_DIR}/README.md --lua ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested/hyprland.lua
    VERBATIM
)

add_library(liquid-glass SHARED
    src/main.cpp
    src/PluginRuntime.cpp
    src/LiquidMesh.cpp
    src/EdgeTransition.cpp
    src/SolidFill.cpp
    src/LiquidGlassTransformer.cpp
    src/LiquidCloseRenderer.cpp
    src/PaperCrumple.cpp
    src/PaperTakes.cpp
    src/PaperRenderer.cpp
)
# PaperTakes.cpp #embeds the baked crumples; rebuild it whenever they are re-packed.
set_source_files_properties(src/PaperTakes.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/paper_takes.bin)
set_target_properties(liquid-glass PROPERTIES PREFIX "" INTERPROCEDURAL_OPTIMIZATION FALSE)
add_dependencies(liquid-glass generate-config)
target_include_directories(liquid-glass PRIVATE ${CONFIG_GENERATED_DIR})
target_link_libraries(liquid-glass PRIVATE PkgConfig::HYPRLAND)
# GNU-unique inline statics otherwise make dlclose retain stale plugin code.
target_compile_options(liquid-glass PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique> -Wall -Wextra -Wpedantic)

find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting C++ sources"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking C++ formatting"
        VERBATIM
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME config-docs-check COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA} --readme ${CMAKE_CURRENT_SOURCE_DIR}/README.md --lua ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested/hyprland.lua --check)
    add_test(NAME config-generator-tests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_config_generator.py)
    add_executable(liquid-mesh-tests tests/LiquidMeshTests.cpp src/LiquidMesh.cpp)
    target_include_directories(liquid-mesh-tests PRIVATE src)
    target_compile_options(liquid-mesh-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME liquid-mesh-tests COMMAND liquid-mesh-tests)

    add_executable(edge-transition-tests tests/EdgeTransitionTests.cpp src/EdgeTransition.cpp src/LiquidMesh.cpp)
    target_include_directories(edge-transition-tests PRIVATE src)
    target_compile_options(edge-transition-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME edge-transition-tests COMMAND edge-transition-tests)

    add_executable(paper-crumple-tests tests/PaperCrumpleTests.cpp src/PaperCrumple.cpp src/PaperTakes.cpp)
    target_include_directories(paper-crumple-tests PRIVATE src)
    target_compile_options(paper-crumple-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME paper-crumple-tests COMMAND paper-crumple-tests)
endif()
