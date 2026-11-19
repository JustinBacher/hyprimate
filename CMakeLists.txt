cmake_minimum_required(VERSION 3.25)
project(hyprimate VERSION 0.1.0 LANGUAGES CXX)

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

# Jolt Physics drives the cloth animations. Only the library is built, single precision, statically into the plugin;
# its compute backends (GPU hair), debug renderer, profiler and floating-point exceptions stay off: an FP trap or a
# debug hook inside the compositor would take the session down.
include(FetchContent)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# A compositor plugin must not raise SIGILL on CPUs without AVX2 (Jolt picks AVX-512 first when it is on).
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
set(USE_SSE4_1 ON CACHE BOOL "" FORCE)
set(USE_SSE4_2 ON CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    URL_HASH SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
    SOURCE_SUBDIR Build
    SYSTEM
)
FetchContent_MakeAvailable(JoltPhysics)

# Keep project warnings out of Jolt's already-configured subdirectory.
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wunused-function -Wunused-variable
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wunused-private-field>
)

set(CONFIG_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/config/settings.json)
set(CONFIG_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_config.py)
set(CONFIG_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/config_generated.hpp
           ${CONFIG_GENERATED_DIR}/config_settings_generated.hpp
    COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA}
            --header ${CONFIG_GENERATED_DIR}/config_generated.hpp
            --settings-header ${CONFIG_GENERATED_DIR}/config_settings_generated.hpp
    DEPENDS ${CONFIG_SCHEMA} ${CONFIG_GENERATOR}
    VERBATIM
)
add_custom_target(generate-config DEPENDS ${CONFIG_GENERATED_DIR}/config_generated.hpp
    ${CONFIG_GENERATED_DIR}/config_settings_generated.hpp)
add_custom_target(update-config-docs
    COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA} --readme ${CMAKE_CURRENT_SOURCE_DIR}/README.md --lua ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested/hyprland.lua
    VERBATIM
)

# Hyprland-independent core: liquid mesh, edge transition, paper, cloth and swarm models. The plugin and the tests
# link it.
add_library(hyprimate-core STATIC
    src/animations/shared/EdgeTransition.cpp
    src/animations/shared/SheetFrame.cpp
    src/workspace/WorkspaceTransition.cpp
    src/workspace/WorkspaceController.cpp
    src/WindowTracker.cpp
)
target_include_directories(hyprimate-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hyprimate-core PRIVATE Jolt)
target_compile_options(hyprimate-core PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)

add_library(hyprimate SHARED
    src/main.cpp
    src/PluginRuntime.cpp
    src/AnimationTransformer.cpp
    src/AnimationCloseRenderer.cpp
    src/HyprWindowPort.cpp
    src/HyprAnimationCanvas.cpp
    src/WindowAnimationPass.cpp
    src/StyleContract.cpp
    src/NativeFadeoutPass.cpp
    src/animations/shared/SheetRenderer.cpp
    src/animations/shared/SolidFill.cpp
    src/workspace/WorkspaceCapture.cpp
    src/workspace/WorkspaceAnimation.cpp
)
set_target_properties(hyprimate PROPERTIES PREFIX "" INTERPROCEDURAL_OPTIMIZATION FALSE)
add_dependencies(hyprimate generate-config)
target_include_directories(hyprimate PRIVATE ${CONFIG_GENERATED_DIR})
target_link_libraries(hyprimate PRIVATE PkgConfig::HYPRLAND Jolt hyprimate-core)
# Keep Jolt's symbols out of the plugin's dynamic symbol table, so they cannot bind to another copy in the process.
target_link_options(hyprimate PRIVATE -Wl,--exclude-libs,ALL)
# GNU-unique inline statics otherwise make dlclose retain stale plugin code.
target_compile_options(hyprimate PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)

# Shared plugins may import host APIs, but every strong import must have a provider.
find_program(HYPRLAND_EXECUTABLE NAMES Hyprland REQUIRED)
set(PLUGIN_SYMBOL_CHECKER ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_plugin_symbols.py)
set_property(TARGET hyprimate APPEND PROPERTY LINK_DEPENDS
    ${PLUGIN_SYMBOL_CHECKER} ${HYPRLAND_EXECUTABLE})
add_custom_command(TARGET hyprimate POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${PLUGIN_SYMBOL_CHECKER}
            $<TARGET_FILE:hyprimate> ${HYPRLAND_EXECUTABLE} --nm ${CMAKE_NM}
    COMMENT "Checking plugin imports against Hyprland and shared dependencies"
    VERBATIM
)

add_custom_target(clang-tidy-check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_clang_tidy.py
            --build-dir ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking project translation units with clang-tidy"
    DEPENDS generate-config
    VERBATIM
)

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
    # Surfaceless EGL for the GL material tests any Style may add.
    pkg_check_modules(TEST_EGL REQUIRED IMPORTED_TARGET egl glesv2)
endif()
set(HYPRIMATE_STYLES liquid paper cloth swarm bubble smoke)
foreach(style IN LISTS HYPRIMATE_STYLES)
    include(src/animations/${style}/Style.cmake)
endforeach()
set(HYPRIMATE_WORKSPACE_STYLES rotation)
foreach(style IN LISTS HYPRIMATE_WORKSPACE_STYLES)
    include(src/workspace/${style}/Style.cmake)
endforeach()
if(BUILD_TESTING)
    add_test(NAME config-docs-check COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA} --readme ${CMAKE_CURRENT_SOURCE_DIR}/README.md --lua ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested/hyprland.lua --check)
    add_test(NAME config-generator-tests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_config_generator.py)
    add_test(NAME plugin-symbol-tests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_plugin_symbols.py)
    add_test(NAME style-independence-tests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_style_independence.py)
    add_executable(config-inheritance-tests tests/ConfigInheritanceTests.cpp)
    add_dependencies(config-inheritance-tests generate-config)
    target_include_directories(config-inheritance-tests PRIVATE ${CONFIG_GENERATED_DIR})
    add_test(NAME config-inheritance-tests COMMAND config-inheritance-tests)

    add_executable(edge-transition-tests tests/EdgeTransitionTests.cpp)
    target_link_libraries(edge-transition-tests PRIVATE hyprimate-core)
    target_include_directories(edge-transition-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME edge-transition-tests COMMAND edge-transition-tests)

    add_executable(workspace-transition-tests src/workspace/tests/WorkspaceTransitionTests.cpp)
    target_link_libraries(workspace-transition-tests PRIVATE hyprimate-core)
    target_include_directories(workspace-transition-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME workspace-transition-tests COMMAND workspace-transition-tests)

    add_executable(workspace-controller-tests src/workspace/tests/WorkspaceControllerTests.cpp)
    target_link_libraries(workspace-controller-tests PRIVATE hyprimate-core)
    target_include_directories(workspace-controller-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME workspace-controller-tests COMMAND workspace-controller-tests)


    add_executable(window-tracker-tests tests/WindowTrackerTests.cpp)
    target_link_libraries(window-tracker-tests PRIVATE hyprimate-core)
    add_test(NAME window-tracker-tests COMMAND window-tracker-tests)

    add_executable(window-animations-tests tests/WindowAnimationsTests.cpp)
    add_dependencies(window-animations-tests generate-config)
    target_link_libraries(window-animations-tests PRIVATE hyprimate-core)
    target_include_directories(window-animations-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CONFIG_GENERATED_DIR})
    add_test(NAME window-animations-tests COMMAND window-animations-tests)

    add_library(window-mesh-abi-tests OBJECT tests/WindowMeshAbiTests.cpp)
    target_include_directories(window-mesh-abi-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(window-mesh-abi-tests PRIVATE PkgConfig::HYPRLAND)

    add_executable(sheet-mesh-tests tests/SheetMeshTests.cpp)
    target_link_libraries(sheet-mesh-tests PRIVATE hyprimate-core)
    target_include_directories(sheet-mesh-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME sheet-mesh-tests COMMAND sheet-mesh-tests)

    add_executable(slot-pool-tests tests/SlotPoolTests.cpp)
    target_include_directories(slot-pool-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME slot-pool-tests COMMAND slot-pool-tests)

endif()
