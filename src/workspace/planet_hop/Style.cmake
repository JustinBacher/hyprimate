find_package(PNG REQUIRED)
target_sources(hyprimate-core PRIVATE src/workspace/planet_hop/PlanetScene.cpp)
add_library(planet-gl STATIC
    src/workspace/planet_hop/PlanetGlRenderer.cpp
    src/workspace/planet_hop/PlanetGlResources.cpp
    src/workspace/planet_hop/PlanetDepth.cpp)
target_include_directories(planet-gl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# depth_source is a production setting, not evidence tooling: retain bounded PNG decoding.
target_link_libraries(planet-gl PUBLIC hyprimate-core PRIVATE PNG::PNG)
target_compile_options(planet-gl PRIVATE -frtti $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
pkg_check_modules(PLANET_LUA REQUIRED IMPORTED_TARGET lua)
target_sources(hyprimate PRIVATE src/workspace/planet_hop/PlanetRenderer.cpp src/workspace/planet_hop/PlanetCurves.cpp)
target_link_libraries(hyprimate PRIVATE planet-gl PkgConfig::PLANET_LUA)

if(BUILD_TESTING)
    pkg_check_modules(PLANET_HYPRUTILS REQUIRED IMPORTED_TARGET hyprutils)
    add_executable(planet-spring-tests src/workspace/planet_hop/tests/PlanetSpringTests.cpp)
    target_include_directories(planet-spring-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(planet-spring-tests PRIVATE hyprimate-core PkgConfig::PLANET_HYPRUTILS)
    add_test(NAME planet-spring-tests COMMAND planet-spring-tests)

    add_executable(planet-scene-tests src/workspace/planet_hop/tests/PlanetSceneTests.cpp)
    target_link_libraries(planet-scene-tests PRIVATE hyprimate-core)
    target_include_directories(planet-scene-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME planet-scene-tests COMMAND planet-scene-tests)

    add_executable(planet-material-tests tests/PlanetMaterialTests.cpp)
    target_include_directories(planet-material-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(planet-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME planet-material-tests COMMAND planet-material-tests)
    set_tests_properties(planet-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)

    # Only the offscreen profiler needs the typed 64-bit GLES timer-query entry point.
    # libGLESv2 exports no glGetQueryObjectui64vEXT; libGL supplies the same GL dispatch.
    pkg_check_modules(TEST_PLANET_GL REQUIRED IMPORTED_TARGET gl)
    add_executable(planet-renderer-tests src/workspace/planet_hop/tests/PlanetRendererTests.cpp)
    target_include_directories(planet-renderer-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(planet-renderer-tests PRIVATE GL_GLEXT_PROTOTYPES)
    target_link_libraries(planet-renderer-tests PRIVATE planet-gl PkgConfig::TEST_EGL PkgConfig::TEST_PLANET_GL)
    add_test(NAME planet-renderer-tests COMMAND planet-renderer-tests)
    set_tests_properties(planet-renderer-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)
endif()
