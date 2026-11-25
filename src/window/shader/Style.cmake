find_package(PNG REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
pkg_check_modules(SHADER_GLES REQUIRED IMPORTED_TARGET glesv2)

# Package semantics and GLES execution have no Hyprland or EGL dependency.
add_library(compute-source STATIC src/compute/KernelSource.cpp)
target_include_directories(compute-source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_library(compute-gles STATIC
    src/compute/Device.cpp
    src/compute/FieldStorage.cpp
    src/compute/NativeBackend.cpp
    src/compute/SlibsBackend.cpp)
target_link_libraries(compute-gles PUBLIC compute-source PkgConfig::SHADER_GLES)

add_library(shader-package STATIC src/shader/Package.cpp src/shader/ShaderSource.cpp src/shader/PackageValidation.cpp)
target_include_directories(shader-package PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shader-package PRIVATE PNG::PNG nlohmann_json::nlohmann_json compute-source)
add_library(shader-gles STATIC
    src/shader/GlesRuntime.cpp
    src/shader/GlesState.cpp
    src/shader/GlesProgram.cpp
    src/shader/GlesInstanceStorage.cpp)
target_include_directories(shader-gles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shader-gles PUBLIC shader-package compute-gles)
foreach(target compute-source compute-gles shader-package shader-gles)
    target_compile_options(${target} PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
endforeach()

option(HYPRIMATE_COMPUTE_BENCH_ADAPTER "Build the nested CPU comparator into a test-only plugin" OFF)
if(HYPRIMATE_COMPUTE_BENCH_ADAPTER)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "HYPRIMATE_COMPUTE_BENCH_ADAPTER requires BUILD_TESTING")
    endif()
    target_sources(shader-gles PRIVATE src/compute/tests/NestedCpuAdapter.cpp)
    target_compile_definitions(shader-gles PUBLIC HYPRIMATE_COMPUTE_BENCH_ADAPTER)
    target_compile_definitions(shader-gles PRIVATE
        HYPRIMATE_SHADER_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders")
endif()

target_sources(hyprimate PRIVATE
    src/window/shader/ShaderLiveStyle.cpp
    src/window/shader/ShaderRenderer.cpp
    src/shader/hyprland/HyprShaderProgram.cpp
    src/shader/hyprland/HyprShaderTarget.cpp
    src/shader/hyprland/SceneShader.cpp
    src/shader/hyprland/HyprEnvironmentInteractions.cpp
    src/shader/hyprland/HyprWallpaperSource.cpp
    src/shader/hyprland/ShaderWallpaper.cpp)
target_link_libraries(hyprimate PRIVATE shader-gles)

if(BUILD_TESTING)
    add_executable(compute-example examples/compute/main.cpp)
    target_link_libraries(compute-example PRIVATE compute-gles PkgConfig::TEST_EGL)
    target_include_directories(compute-example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME compute-example COMMAND compute-example)
    set_tests_properties(compute-example PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")

    add_executable(compute-package-example-tests src/compute/tests/PackageExampleProof.cpp)
    target_link_libraries(compute-package-example-tests PRIVATE shader-gles PkgConfig::TEST_EGL)
    target_include_directories(compute-package-example-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME compute-package-example-tests COMMAND compute-package-example-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders")
    set_tests_properties(compute-package-example-tests PROPERTIES
        ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)

    add_executable(compute-extended-state-tests src/compute/tests/ExtendedStateTests.cpp)
    target_link_libraries(compute-extended-state-tests PRIVATE shader-gles PkgConfig::TEST_EGL)
    target_link_options(compute-extended-state-tests PRIVATE "LINKER:--wrap=glTexStorage2D")
    target_include_directories(compute-extended-state-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME compute-extended-state-tests COMMAND compute-extended-state-tests)
    set_tests_properties(compute-extended-state-tests PROPERTIES
        ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)

    add_executable(compute-tests src/compute/tests/ComputeTests.cpp)
    target_link_libraries(compute-tests PRIVATE compute-gles PkgConfig::TEST_EGL)
    target_link_options(compute-tests PRIVATE
        "LINKER:--wrap=glCreateShader"
        "LINKER:--wrap=glTexStorage2D")
    target_include_directories(compute-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME compute-tests COMMAND compute-tests)
    set_tests_properties(compute-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)
    add_test(NAME compute-es30-tests COMMAND compute-tests --require-es30)
    add_test(NAME compute-es31-tests COMMAND compute-tests --require-es31)
    add_test(NAME compute-es30-no-float-tests COMMAND compute-tests --require-es30 --without-float-render)
    set_tests_properties(compute-es30-tests compute-es30-no-float-tests PROPERTIES
        ENVIRONMENT "EGL_PLATFORM=surfaceless;MESA_GLES_VERSION_OVERRIDE=3.0;LIBGL_ALWAYS_SOFTWARE=true"
        SKIP_RETURN_CODE 77)
    set_tests_properties(compute-es31-tests PROPERTIES
        ENVIRONMENT "EGL_PLATFORM=surfaceless;MESA_GLES_VERSION_OVERRIDE=3.1;LIBGL_ALWAYS_SOFTWARE=true"
        SKIP_RETURN_CODE 77)
    set_property(TEST compute-es30-no-float-tests APPEND PROPERTY ENVIRONMENT
        "MESA_EXTENSION_OVERRIDE=-GL_EXT_color_buffer_float -GL_EXT_color_buffer_half_float")

    add_executable(compute-bench src/compute/tests/ComputeBench.cpp)
    target_link_libraries(compute-bench PRIVATE compute-gles PkgConfig::TEST_EGL)
    target_include_directories(compute-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(compute-bench PRIVATE
        HYPRIMATE_SHADER_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders")

    add_executable(shader-package-tests src/shader/tests/PackageTests.cpp)
    target_link_libraries(shader-package-tests PRIVATE shader-package)
    target_include_directories(shader-package-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME shader-package-tests COMMAND shader-package-tests)

    add_executable(shader-gles-tests src/shader/tests/GlesRuntimeTests.cpp)
    target_link_libraries(shader-gles-tests PRIVATE shader-gles PkgConfig::TEST_EGL)
    target_include_directories(shader-gles-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME shader-gles-tests COMMAND shader-gles-tests)
    set_tests_properties(shader-gles-tests PROPERTIES
        ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)
    add_test(NAME shader-gles-es30-tests COMMAND shader-gles-tests --require-es30)
    set_tests_properties(shader-gles-es30-tests PROPERTIES
        ENVIRONMENT "EGL_PLATFORM=surfaceless;MESA_GLES_VERSION_OVERRIDE=3.0;LIBGL_ALWAYS_SOFTWARE=true"
        SKIP_RETURN_CODE 77)
    find_file(SHADER_MESA_EGL_VENDOR_JSON 50_mesa.json
        PATHS /usr/share/glvnd/egl_vendor.d /usr/local/share/glvnd/egl_vendor.d
        NO_DEFAULT_PATH)
    if(SHADER_MESA_EGL_VENDOR_JSON)
        set_property(TEST shader-gles-es30-tests compute-es30-tests compute-es31-tests
            compute-es30-no-float-tests APPEND PROPERTY ENVIRONMENT
            "__EGL_VENDOR_LIBRARY_FILENAMES=${SHADER_MESA_EGL_VENDOR_JSON}")
    endif()

    add_executable(shader-runtime-bench src/shader/tests/RuntimeBench.cpp)
    target_link_libraries(shader-runtime-bench PRIVATE shader-gles PkgConfig::TEST_EGL)
    target_include_directories(shader-runtime-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(shader-runtime-bench PRIVATE
        HYPRIMATE_SHADER_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders")
endif()
