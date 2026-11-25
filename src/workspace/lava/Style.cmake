target_sources(hyprimate PRIVATE src/workspace/lava/LavaRenderer.cpp src/workspace/lava/LavaCapture.cpp)

if(BUILD_TESTING)
    add_executable(lava-renderer-tests src/workspace/lava/tests/LavaRendererTests.cpp
        src/workspace/lava/LavaRenderer.cpp)
    target_compile_definitions(lava-renderer-tests PRIVATE HYPRIMATE_LAVA_RAW_ONLY)
    target_include_directories(lava-renderer-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(lava-renderer-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME lava-renderer-tests COMMAND lava-renderer-tests)
    set_tests_properties(lava-renderer-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
endif()
