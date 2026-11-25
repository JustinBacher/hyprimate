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

option(BUILD_TESTING "Build Hyprimate's development tests" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wunused-function -Wunused-variable
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wunused-private-field>
)

set(CONFIG_SNAPSHOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config/generated)
set(CONFIG_SNAPSHOTS
    ${CONFIG_SNAPSHOT_DIR}/config_generated.hpp
    ${CONFIG_SNAPSHOT_DIR}/config_settings_generated.hpp
)
set(CONFIG_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/config_generated.hpp
           ${CONFIG_GENERATED_DIR}/config_settings_generated.hpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CONFIG_GENERATED_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_SNAPSHOTS} ${CONFIG_GENERATED_DIR}
    DEPENDS ${CONFIG_SNAPSHOTS}
    VERBATIM
)
add_custom_target(generate-config DEPENDS ${CONFIG_GENERATED_DIR}/config_generated.hpp
    ${CONFIG_GENERATED_DIR}/config_settings_generated.hpp)

if(BUILD_TESTING)
    set(CONFIG_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_config.py)
    set(CONFIG_GENERATOR_ARGS
        --schema ${CMAKE_CURRENT_SOURCE_DIR}/config/settings.json
        --header ${CONFIG_SNAPSHOT_DIR}/config_generated.hpp
        --settings-header ${CONFIG_SNAPSHOT_DIR}/config_settings_generated.hpp
        --readme ${CMAKE_CURRENT_SOURCE_DIR}/README.md
        --lua ${CMAKE_CURRENT_SOURCE_DIR}/tests/nested/hyprland.lua
    )
    add_custom_target(update-config
        COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} ${CONFIG_GENERATOR_ARGS}
        VERBATIM
    )
endif()

# Hyprland-independent core: liquid mesh, edge transition, paper, cloth and swarm models. The plugin and the tests
# link it.
add_library(hyprimate-core STATIC
    src/window/WindowAnimationClock.cpp
    src/window/WindowAnimationGeometry.cpp
    src/window/shared/EdgeFrame.cpp
    src/window/shared/SheetFrame.cpp
    src/workspace/WorkspaceTransition.cpp
    src/window/WindowTracker.cpp
    src/perf/LiveProfiler.cpp
)
target_include_directories(hyprimate-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(hyprimate-core PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)

add_library(hyprimate SHARED
    src/main.cpp
    src/plugin/PluginRuntime.cpp
    src/perf/GpuStopwatch.cpp
    src/perf/GpuTimer.cpp
    src/perf/RuntimeProfiler.cpp
    src/window/hyprland/AnimationTransformer.cpp
    src/window/hyprland/AnimationCloseRenderer.cpp
    src/window/hyprland/HyprWindowIdentity.cpp
    src/window/hyprland/AnimationCloseHooks.cpp
    src/window/hyprland/CloseRenderGuard.cpp
    src/window/hyprland/AnimationCloseAssociation.cpp
    src/window/hyprland/HyprWindowPort.cpp
    src/window/hyprland/HyprWindowVisuals.cpp
    src/window/hyprland/HyprAnimationCanvas.cpp
    src/window/hyprland/HyprglassCompat.cpp
    src/window/hyprland/WindowAnimationPass.cpp
    src/window/hyprland/StyleContract.cpp
    src/window/hyprland/NativeFadeoutPass.cpp
    src/gl/CompositorContext.cpp
    src/gl/IndexedMesh.cpp
    src/gl/NativeResource.cpp
    src/window/shared/SheetRenderer.cpp
    src/window/shared/SolidFill.cpp
    src/workspace/WorkspaceCapture.cpp
    src/workspace/WorkspaceAnimation.cpp
    src/workspace/WorkspaceCaptureBuffers.cpp
    src/workspace/WorkspacePreviews.cpp
    src/workspace/WorkspaceStyleDispatch.cpp
)
set_target_properties(hyprimate PROPERTIES PREFIX "" INTERPROCEDURAL_OPTIMIZATION FALSE)
add_dependencies(hyprimate generate-config)
target_include_directories(hyprimate PRIVATE ${CONFIG_GENERATED_DIR})
target_link_libraries(hyprimate PRIVATE PkgConfig::HYPRLAND hyprimate-core ${CMAKE_DL_LIBS})
# Internal static libraries must not interpose on symbols in the compositor process.
target_link_options(hyprimate PRIVATE -Wl,--exclude-libs,ALL)
# GNU-unique inline statics otherwise make dlclose retain stale plugin code.
target_compile_options(hyprimate PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)

# Shared plugins may import host APIs, but every strong import must have a provider.
find_program(HYPRLAND_EXECUTABLE NAMES Hyprland REQUIRED)
set(PLUGIN_SYMBOL_CHECKER ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_plugin_symbols.cmake)
set_property(TARGET hyprimate APPEND PROPERTY LINK_DEPENDS
    ${PLUGIN_SYMBOL_CHECKER} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_plugin_symbols.sh
    ${HYPRLAND_EXECUTABLE})
add_custom_command(TARGET hyprimate POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DPLUGIN=$<TARGET_FILE:hyprimate>" "-DHOST=${HYPRLAND_EXECUTABLE}" "-DNM=${CMAKE_NM}"
            -P ${PLUGIN_SYMBOL_CHECKER}
    COMMENT "Checking plugin imports against Hyprland and shared dependencies"
    VERBATIM
)

if(BUILD_TESTING)
    add_custom_target(clang-tidy-check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_clang_tidy.py
                --build-dir ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking project translation units with clang-tidy"
        DEPENDS generate-config
        VERBATIM
    )
endif()

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
    pkg_check_modules(PLANET_LUA REQUIRED IMPORTED_TARGET lua)
endif()
set(HYPRIMATE_STYLES liquid paper cloth swarm bubble smoke burn construction shader)
foreach(style IN LISTS HYPRIMATE_STYLES)
    include(src/window/${style}/Style.cmake)
endforeach()
set(HYPRIMATE_WORKSPACE_STYLES rotation planet_hop cube lava)
foreach(style IN LISTS HYPRIMATE_WORKSPACE_STYLES)
    include(src/workspace/${style}/Style.cmake)
endforeach()
if(BUILD_TESTING)
    add_executable(live-profiler-probe tests/LiveProfilerProbe.cpp)
    target_link_libraries(live-profiler-probe PRIVATE hyprimate-core)
    add_test(NAME live-profiler-tests COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_live_profiler.py $<TARGET_FILE:live-profiler-probe>)
    add_executable(gpu-timer-tests tests/GpuTimerTests.cpp src/perf/GpuTimer.cpp)
    target_include_directories(gpu-timer-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(gpu-timer-tests PRIVATE PkgConfig::TEST_EGL Threads::Threads)
    target_link_options(gpu-timer-tests PRIVATE "LINKER:--wrap=glGetIntegerv")
    add_test(NAME gpu-timer-tests COMMAND gpu-timer-tests)
    set_tests_properties(gpu-timer-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)
    add_executable(raw-gl-state-tests tests/RawGlStateTests.cpp)
    target_include_directories(raw-gl-state-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(raw-gl-state-tests PRIVATE PkgConfig::TEST_EGL)
    add_test(NAME raw-gl-state-tests COMMAND raw-gl-state-tests)
    add_test(NAME raw-gl-state-es30-tests COMMAND raw-gl-state-tests --require-es30)
    set_tests_properties(raw-gl-state-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
    set_tests_properties(raw-gl-state-es30-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless;MESA_GLES_VERSION_OVERRIDE=3.0")
    set_tests_properties(raw-gl-state-es30-tests PROPERTIES SKIP_RETURN_CODE 77)
    # On mixed-vendor hosts the default EGL implementation may ignore Mesa's version override.
    find_file(MESA_EGL_VENDOR_JSON 50_mesa.json PATH_SUFFIXES glvnd/egl_vendor.d PATHS /usr/local/share /usr/share)
    if(MESA_EGL_VENDOR_JSON)
        set_property(TEST raw-gl-state-es30-tests APPEND PROPERTY ENVIRONMENT "__EGL_VENDOR_LIBRARY_FILENAMES=${MESA_EGL_VENDOR_JSON}" "LIBGL_ALWAYS_SOFTWARE=true")
        set_property(TEST construction-material-es30-tests APPEND PROPERTY ENVIRONMENT "__EGL_VENDOR_LIBRARY_FILENAMES=${MESA_EGL_VENDOR_JSON}" "LIBGL_ALWAYS_SOFTWARE=true")
    endif()
    # Inject one failed storage request into the test executable, not the plugin or its
    # driver. All other calls use the real surfaceless context and production material.
    add_library(gl-allocation-fault STATIC tests/GlAllocationFault.cpp)
    target_link_libraries(gl-allocation-fault PRIVATE PkgConfig::TEST_EGL)
    foreach(call glTexStorage2D glTexStorage3D glRenderbufferStorage glBufferData)
        target_link_options(gl-allocation-fault INTERFACE "LINKER:--wrap=${call}")
    endforeach()
    foreach(target construction-material-tests burn-material-tests paper-material-tests smoke-material-tests bubble-material-tests planet-renderer-tests)
        target_link_libraries(${target} PRIVATE gl-allocation-fault)
    endforeach()
    add_executable(gl-storage-tests tests/GlStorageTests.cpp)
    target_include_directories(gl-storage-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(gl-storage-tests PRIVATE PkgConfig::TEST_EGL)
    add_test(NAME gl-storage-tests COMMAND gl-storage-tests)
    set_tests_properties(gl-storage-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
    find_package(Threads REQUIRED)
    add_executable(gl-context-tests tests/GlContextTests.cpp)
    target_include_directories(gl-context-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(gl-context-tests PRIVATE PkgConfig::TEST_EGL Threads::Threads)
    add_test(NAME gl-context-tests COMMAND gl-context-tests)
    set_tests_properties(gl-context-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")

    add_test(NAME config-generated-check
        COMMAND ${Python3_EXECUTABLE} ${CONFIG_GENERATOR} ${CONFIG_GENERATOR_ARGS} --check)
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

    add_executable(workspace-turn-tests src/workspace/tests/WorkspaceTurnTests.cpp)
    target_link_libraries(workspace-turn-tests PRIVATE hyprimate-core)
    target_include_directories(workspace-turn-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME workspace-turn-tests COMMAND workspace-turn-tests)

    add_executable(workspace-inventory-tests src/workspace/tests/WorkspaceInventoryTests.cpp)
    target_link_libraries(workspace-inventory-tests PRIVATE hyprimate-core)
    target_include_directories(workspace-inventory-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME workspace-inventory-tests COMMAND workspace-inventory-tests)


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

    add_library(plugin-import-fixture SHARED tests/PluginImportFixture.cpp)
    target_link_options(plugin-import-fixture PRIVATE -Wl,-z,lazy,-z,relro)
    add_library(plugin-import-protected-fixture SHARED tests/PluginImportFixture.cpp)
    target_link_options(plugin-import-protected-fixture PRIVATE -Wl,-z,now,-z,relro)
    add_executable(plugin-import-tests tests/PluginImportTests.cpp)
    target_include_directories(plugin-import-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(plugin-import-tests PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(plugin-import-tests plugin-import-fixture plugin-import-protected-fixture)
    add_test(NAME plugin-import-tests COMMAND plugin-import-tests
        $<TARGET_FILE:plugin-import-fixture> $<TARGET_FILE:plugin-import-protected-fixture>)

    add_executable(slot-pool-tests tests/SlotPoolTests.cpp)
    target_include_directories(slot-pool-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME slot-pool-tests COMMAND slot-pool-tests)

    add_library(workspace-swipe-probe SHARED tests/nested/WorkspaceSwipeProbe.cpp)
    set_target_properties(workspace-swipe-probe PROPERTIES PREFIX "")
    target_link_libraries(workspace-swipe-probe PRIVATE PkgConfig::HYPRLAND PkgConfig::PLANET_LUA)

endif()
