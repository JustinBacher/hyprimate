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

set(CONFIG_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/config/settings.json)
set(CONFIG_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_config.py)
set(CONFIG_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/config_generated.hpp
    COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA}
            --header ${CONFIG_GENERATED_DIR}/config_generated.hpp
    DEPENDS ${CONFIG_SCHEMA} ${CONFIG_GENERATOR}
    VERBATIM
)
add_custom_target(generate-config DEPENDS ${CONFIG_GENERATED_DIR}/config_generated.hpp)
add_custom_target(update-config-docs
    COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA} --readme ${CMAKE_CURRENT_SOURCE_DIR}/README.md --lua ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested/hyprland.lua
    VERBATIM
)

# Hyprland-independent core: liquid mesh, edge transition, paper and cloth models. The plugin and the tests link it.
add_library(liquid-glass-core STATIC
    src/LiquidPose.cpp
    src/LiquidMesh.cpp
    src/EdgeTransition.cpp
    src/PaperCrumple.cpp
    src/PaperTakes.cpp
    src/SheetFrame.cpp
    src/ClothFlutter.cpp
    src/WindowTracker.cpp
)
# PaperTakes.cpp #embeds the baked crumples; rebuild it whenever they are re-packed.
set_source_files_properties(src/PaperTakes.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/paper_takes.bin)
set_source_files_properties(src/ClothFlutter.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")
target_include_directories(liquid-glass-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(liquid-glass-core PRIVATE Jolt)
target_compile_options(liquid-glass-core PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique> -Wall -Wextra -Wpedantic)

add_library(liquid-glass SHARED
    src/main.cpp
    src/PluginRuntime.cpp
    src/SolidFill.cpp
    src/LiquidGlassTransformer.cpp
    src/LiquidCloseRenderer.cpp
    src/PaperRenderer.cpp
)
set_target_properties(liquid-glass PROPERTIES PREFIX "" INTERPROCEDURAL_OPTIMIZATION FALSE)
add_dependencies(liquid-glass generate-config)
target_include_directories(liquid-glass PRIVATE ${CONFIG_GENERATED_DIR})
target_link_libraries(liquid-glass PRIVATE PkgConfig::HYPRLAND Jolt liquid-glass-core)
# Keep Jolt's symbols out of the plugin's dynamic symbol table, so they cannot bind to another copy in the process.
target_link_options(liquid-glass PRIVATE -Wl,--exclude-libs,ALL)
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
    add_executable(liquid-mesh-tests tests/LiquidMeshTests.cpp)
    target_link_libraries(liquid-mesh-tests PRIVATE liquid-glass-core)
    target_compile_options(liquid-mesh-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME liquid-mesh-tests COMMAND liquid-mesh-tests)

    add_executable(edge-transition-tests tests/EdgeTransitionTests.cpp)
    target_link_libraries(edge-transition-tests PRIVATE liquid-glass-core)
    target_compile_options(edge-transition-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME edge-transition-tests COMMAND edge-transition-tests)

    add_executable(paper-crumple-tests tests/PaperCrumpleTests.cpp)
    target_link_libraries(paper-crumple-tests PRIVATE liquid-glass-core)
    target_compile_options(paper-crumple-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME paper-crumple-tests COMMAND paper-crumple-tests)

    add_executable(cloth-flutter-tests tests/ClothFlutterTests.cpp)
    target_link_libraries(cloth-flutter-tests PRIVATE liquid-glass-core Jolt)
    target_compile_options(cloth-flutter-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME cloth-flutter-tests COMMAND cloth-flutter-tests)

    add_executable(window-tracker-tests tests/WindowTrackerTests.cpp)
    target_link_libraries(window-tracker-tests PRIVATE liquid-glass-core)
    target_compile_options(window-tracker-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME window-tracker-tests COMMAND window-tracker-tests)
endif()
