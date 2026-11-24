find_package(PNG REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
pkg_check_modules(SHADER_GLES REQUIRED IMPORTED_TARGET glesv2)

# Package semantics and GLES execution have no Hyprland or EGL dependency.
add_library(shader-package STATIC src/shader/Package.cpp)
target_include_directories(shader-package PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shader-package PRIVATE PNG::PNG nlohmann_json::nlohmann_json)
add_library(shader-gles STATIC src/shader/GlesRuntime.cpp)
target_include_directories(shader-gles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shader-gles PUBLIC shader-package PkgConfig::SHADER_GLES)
foreach(target shader-package shader-gles)
    target_compile_options(${target} PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
endforeach()

target_sources(hyprimate PRIVATE
    src/window/shader/ShaderLiveStyle.cpp
    src/window/shader/ShaderRenderer.cpp)
target_link_libraries(hyprimate PRIVATE shader-gles)

if(BUILD_TESTING)
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
        set_property(TEST shader-gles-es30-tests APPEND PROPERTY ENVIRONMENT
            "__EGL_VENDOR_LIBRARY_FILENAMES=${SHADER_MESA_EGL_VENDOR_JSON}")
    endif()

    add_executable(shader-runtime-bench src/shader/tests/RuntimeBench.cpp)
    target_link_libraries(shader-runtime-bench PRIVATE shader-gles PkgConfig::TEST_EGL)
    target_include_directories(shader-runtime-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(shader-runtime-bench PRIVATE
        HYPRIMATE_SHADER_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders")
endif()
