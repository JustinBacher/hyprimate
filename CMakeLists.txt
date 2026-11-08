cmake_minimum_required(VERSION 3.25)
project(liquid-glass VERSION 0.1.0 LANGUAGES CXX)

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
)
set_target_properties(liquid-glass PROPERTIES PREFIX "" INTERPROCEDURAL_OPTIMIZATION FALSE)
add_dependencies(liquid-glass generate-config)
target_include_directories(liquid-glass PRIVATE ${CONFIG_GENERATED_DIR})
target_link_libraries(liquid-glass PRIVATE PkgConfig::HYPRLAND)
# GNU-unique inline statics otherwise make dlclose retain stale plugin code.
target_compile_options(liquid-glass PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique> -Wall -Wextra -Wpedantic)

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
endif()
