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
set(BUBBLE_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/animations/bubble/shaders)
set(BUBBLE_SHADER_FILES
    ${BUBBLE_SHADER_DIR}/membrane.vert
    ${BUBBLE_SHADER_DIR}/membrane.frag
    ${BUBBLE_SHADER_DIR}/curl.vert
    ${BUBBLE_SHADER_DIR}/curl.frag
    ${BUBBLE_SHADER_DIR}/particle.vert
    ${BUBBLE_SHADER_DIR}/particle.frag
)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
            --source-dir ${BUBBLE_SHADER_DIR}
            --output ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp
    DEPENDS ${BUBBLE_SHADER_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
    VERBATIM
)
add_custom_target(generate-bubble-shaders DEPENDS ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp)
add_custom_target(update-config-docs
    COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} --schema ${CONFIG_SCHEMA} --readme ${CMAKE_CURRENT_SOURCE_DIR}/README.md --lua ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested/hyprland.lua
    VERBATIM
)

# Hyprland-independent core: liquid mesh, edge transition, paper, cloth and swarm models. The plugin and the tests
# link it.
add_library(hyprimate-core STATIC
    src/animations/liquid/LiquidPose.cpp
    src/animations/liquid/LiquidMesh.cpp
    src/animations/shared/EdgeTransition.cpp
    src/animations/paper/PaperCrumple.cpp
    src/animations/paper/PaperTakes.cpp
    src/animations/shared/SheetFrame.cpp
    src/animations/cloth/ClothFlutter.cpp
    src/animations/swarm/Swarm.cpp
    src/animations/swarm/SwarmSprite.cpp
    src/animations/bubble/Bubble.cpp
    src/animations/bubble/BubbleRupture.cpp
    src/animations/bubble/BubbleOptics.cpp
    src/WindowTracker.cpp
)
# PaperTakes.cpp #embeds the baked crumples; rebuild it whenever they are re-packed.
set_source_files_properties(src/animations/paper/PaperTakes.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/animations/paper/assets/paper_takes.bin)
set_source_files_properties(src/animations/cloth/ClothFlutter.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")
target_include_directories(hyprimate-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hyprimate-core PRIVATE Jolt)
target_compile_options(hyprimate-core PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique> -Wall -Wextra -Wpedantic)

add_library(hyprimate SHARED
    src/main.cpp
    src/PluginRuntime.cpp
    src/animations/liquid/SolidFill.cpp
    src/animations/liquid/LiquidLiveStyle.cpp
    src/animations/paper/PaperLiveStyle.cpp
    src/animations/cloth/ClothLiveStyle.cpp
    src/animations/swarm/SwarmLiveStyle.cpp
    src/animations/swarm/SwarmRenderer.cpp
    src/animations/bubble/BubbleLiveStyle.cpp
    src/animations/bubble/BubbleRenderer.cpp
    src/animations/bubble/BubbleMaterial.cpp
    src/animations/bubble/BubbleResidue.cpp
    src/animations/bubble/BubbleWallpaper.cpp
    src/animations/liquid/LiquidCloseStyle.cpp
    src/animations/shared/SheetClosePass.cpp
    src/AnimationTransformer.cpp
    src/AnimationCloseRenderer.cpp
    src/NativeFadeoutPass.cpp
    src/animations/shared/PaperRenderer.cpp
)
# SwarmRenderer.cpp #embeds the creature sheets; rebuild it whenever one changes.
set_source_files_properties(src/animations/swarm/SwarmRenderer.cpp PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/animations/swarm/sprites/spider.png;${CMAKE_CURRENT_SOURCE_DIR}/src/animations/swarm/sprites/cat.png")
set_target_properties(hyprimate PROPERTIES PREFIX "" INTERPROCEDURAL_OPTIMIZATION FALSE)
add_dependencies(hyprimate generate-config generate-bubble-shaders)
target_include_directories(hyprimate PRIVATE ${CONFIG_GENERATED_DIR})
target_link_libraries(hyprimate PRIVATE PkgConfig::HYPRLAND Jolt hyprimate-core)
# Keep Jolt's symbols out of the plugin's dynamic symbol table, so they cannot bind to another copy in the process.
target_link_options(hyprimate PRIVATE -Wl,--exclude-libs,ALL)
# GNU-unique inline statics otherwise make dlclose retain stale plugin code.
target_compile_options(hyprimate PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique> -Wall -Wextra -Wpedantic)

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
    add_executable(liquid-mesh-tests src/animations/liquid/tests/LiquidMeshTests.cpp)
    target_link_libraries(liquid-mesh-tests PRIVATE hyprimate-core)
    target_include_directories(liquid-mesh-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME liquid-mesh-tests COMMAND liquid-mesh-tests)

    add_executable(edge-transition-tests src/animations/shared/tests/EdgeTransitionTests.cpp)
    target_link_libraries(edge-transition-tests PRIVATE hyprimate-core)
    target_include_directories(edge-transition-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME edge-transition-tests COMMAND edge-transition-tests)

    add_executable(paper-crumple-tests src/animations/paper/tests/PaperCrumpleTests.cpp)
    target_link_libraries(paper-crumple-tests PRIVATE hyprimate-core)
    target_include_directories(paper-crumple-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME paper-crumple-tests COMMAND paper-crumple-tests)

    add_executable(cloth-flutter-tests src/animations/cloth/tests/ClothFlutterTests.cpp)
    target_link_libraries(cloth-flutter-tests PRIVATE hyprimate-core Jolt)
    target_include_directories(cloth-flutter-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME cloth-flutter-tests COMMAND cloth-flutter-tests)

    add_executable(swarm-tests src/animations/swarm/tests/SwarmTests.cpp)
    target_link_libraries(swarm-tests PRIVATE hyprimate-core)
    target_include_directories(swarm-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(swarm-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME swarm-tests COMMAND swarm-tests)

    add_executable(bubble-tests src/animations/bubble/tests/BubbleTests.cpp)
    target_link_libraries(bubble-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(bubble-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME bubble-tests COMMAND bubble-tests)

    add_executable(bubble-optics-tests src/animations/bubble/tests/BubbleOpticsTests.cpp)
    target_link_libraries(bubble-optics-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-optics-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(bubble-optics-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME bubble-optics-tests COMMAND bubble-optics-tests)

    add_executable(window-tracker-tests tests/WindowTrackerTests.cpp)
    target_link_libraries(window-tracker-tests PRIVATE hyprimate-core)
    target_compile_options(window-tracker-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME window-tracker-tests COMMAND window-tracker-tests)

endif()
