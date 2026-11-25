# The hash31 variants in the planet shaders are from the Hash without Sine family
# (David Hoskins, MIT); see THIRD_PARTY_NOTICES.md.
set(PLANET_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/planet_hop/shaders)
set(PLANET_SHADER_EMBEDDER ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.cmake)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/planet_shaders_generated.hpp
    COMMAND ${CMAKE_COMMAND}
            -DSTYLE=planet_hop
            -DSOURCE_DIR=${PLANET_SHADER_DIR}
            -DOUTPUT=${CONFIG_GENERATED_DIR}/planet_shaders_generated.hpp
            -P ${PLANET_SHADER_EMBEDDER}
    DEPENDS ${PLANET_SHADER_DIR}/planet.vert ${PLANET_SHADER_DIR}/planet.frag
            ${PLANET_SHADER_DIR}/quad.vert ${PLANET_SHADER_DIR}/quad.frag
            ${PLANET_SHADER_DIR}/sky.vert ${PLANET_SHADER_DIR}/sky.frag
            ${PLANET_SHADER_EMBEDDER}
    VERBATIM
)
add_custom_target(generate-planet-shaders DEPENDS ${CONFIG_GENERATED_DIR}/planet_shaders_generated.hpp)
add_dependencies(hyprimate generate-planet-shaders)
if(TARGET clang-tidy-check)
    add_dependencies(clang-tidy-check generate-planet-shaders)
endif()

find_package(PNG REQUIRED)
target_sources(hyprimate-core PRIVATE src/workspace/planet_hop/PlanetScene.cpp)
add_library(planet-gl STATIC
    src/workspace/planet_hop/PlanetGlRenderer.cpp
    src/workspace/planet_hop/PlanetGlResources.cpp
    src/workspace/planet_hop/PlanetGlPasses.cpp
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

    add_executable(planet-curves-tests
        src/workspace/planet_hop/tests/PlanetCurvesTests.cpp
        src/workspace/planet_hop/PlanetCurves.cpp)
    target_include_directories(planet-curves-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(planet-curves-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(planet-curves-tests PRIVATE "LINKER:--gc-sections"
        "LINKER:--wrap=_ZN9Hyprutils9Animation17CAnimationManager12bezierExistsERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE")
    target_link_libraries(planet-curves-tests PRIVATE
        hyprimate-core PkgConfig::HYPRLAND PkgConfig::PLANET_LUA PkgConfig::PLANET_HYPRUTILS)
    add_test(NAME planet-curves-tests COMMAND planet-curves-tests)

    add_executable(planet-depth-io-tests
        src/workspace/planet_hop/tests/PlanetDepthIoTests.cpp
        src/workspace/planet_hop/PlanetDepth.cpp)
    target_include_directories(planet-depth-io-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(planet-depth-io-tests PRIVATE PNG::PNG)
    target_link_options(planet-depth-io-tests PRIVATE "LINKER:--wrap=open"
        "LINKER:--wrap=_ZNSt10filesystem9file_sizeERKNS_7__cxx114pathERSt10error_code")
    add_test(NAME planet-depth-io-tests COMMAND planet-depth-io-tests)
    set_tests_properties(planet-depth-io-tests PROPERTIES TIMEOUT 10)

    add_executable(planet-scene-tests src/workspace/planet_hop/tests/PlanetSceneTests.cpp
        src/workspace/planet_hop/tests/PlanetSceneLayoutTests.cpp
        src/workspace/planet_hop/tests/PlanetSceneSurfaceTests.cpp)
    target_link_libraries(planet-scene-tests PRIVATE hyprimate-core)
    target_include_directories(planet-scene-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME planet-scene-tests COMMAND planet-scene-tests)

    add_executable(planet-material-tests tests/PlanetMaterialTests.cpp)
    add_dependencies(planet-material-tests generate-planet-shaders)
    target_include_directories(planet-material-tests PRIVATE ${CONFIG_GENERATED_DIR})
    target_include_directories(planet-material-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(planet-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME planet-material-tests COMMAND planet-material-tests)
    set_tests_properties(planet-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)

    # Only the offscreen profiler needs the typed 64-bit GLES timer-query entry point.
    # libGLESv2 exports no glGetQueryObjectui64vEXT; libGL supplies the same GL dispatch.
    pkg_check_modules(TEST_PLANET_GL REQUIRED IMPORTED_TARGET gl)
    add_executable(planet-renderer-tests src/workspace/planet_hop/tests/PlanetRendererTests.cpp
        src/workspace/planet_hop/tests/PlanetRendererResourceTests.cpp
        src/workspace/planet_hop/tests/PlanetRendererHandoffTests.cpp)
    add_dependencies(planet-renderer-tests generate-planet-shaders)
    target_include_directories(planet-renderer-tests PRIVATE ${CONFIG_GENERATED_DIR})
    target_include_directories(planet-renderer-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(planet-renderer-tests PRIVATE GL_GLEXT_PROTOTYPES)
    target_link_libraries(planet-renderer-tests PRIVATE planet-gl PkgConfig::TEST_EGL PkgConfig::TEST_PLANET_GL)
    add_test(NAME planet-renderer-tests COMMAND planet-renderer-tests)
    set_tests_properties(planet-renderer-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)
endif()
