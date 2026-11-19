target_sources(hyprimate-core PRIVATE src/workspace/rotation/RotationGeometry.cpp)
target_sources(hyprimate PRIVATE src/workspace/rotation/RotationRenderer.cpp)

if(BUILD_TESTING)
    add_executable(rotation-geometry-tests src/workspace/rotation/tests/RotationGeometryTests.cpp)
    target_link_libraries(rotation-geometry-tests PRIVATE hyprimate-core)
    target_include_directories(rotation-geometry-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME rotation-geometry-tests COMMAND rotation-geometry-tests)

    add_executable(rotation-renderer-tests src/workspace/rotation/tests/RotationRendererTests.cpp
        src/workspace/rotation/RotationRenderer.cpp)
    target_compile_definitions(rotation-renderer-tests PRIVATE HYPRIMATE_ROTATION_RAW_ONLY)
    target_include_directories(rotation-renderer-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(rotation-renderer-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME rotation-renderer-tests COMMAND rotation-renderer-tests)
    set_tests_properties(rotation-renderer-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
endif()
