add_library(cube-material STATIC src/workspace/cube/CubeMaterial.cpp)
target_include_directories(cube-material PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(cube-material PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
target_sources(hyprimate PRIVATE src/workspace/cube/CubeRenderer.cpp)
target_link_libraries(hyprimate PRIVATE cube-material)

if(BUILD_TESTING)
    add_executable(cube-renderer-tests src/workspace/cube/tests/CubeRendererTests.cpp)
    target_include_directories(cube-renderer-tests PRIVATE tests tools/cube-preview)
    target_link_libraries(cube-renderer-tests PRIVATE cube-material hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME cube-renderer-tests COMMAND cube-renderer-tests)
    set_tests_properties(cube-renderer-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)

    # The film driver links the same material as the compositor; not installed with the plugin.
    add_executable(cube-production-driver EXCLUDE_FROM_ALL tools/cube-preview/production_driver.cpp)
    target_include_directories(cube-production-driver PRIVATE tests tools/cube-preview)
    target_link_libraries(cube-production-driver PRIVATE cube-material PkgConfig::TEST_EGL)
endif()
