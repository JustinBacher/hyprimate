set(SMOKE_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/window/smoke/shaders)
set(SMOKE_SHADER_EMBEDDER ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.cmake)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/smoke_shaders_generated.hpp
    COMMAND ${CMAKE_COMMAND}
            -DSTYLE=smoke
            -DSOURCE_DIR=${SMOKE_SHADER_DIR}
            -DOUTPUT=${CONFIG_GENERATED_DIR}/smoke_shaders_generated.hpp
            -P ${SMOKE_SHADER_EMBEDDER}
    DEPENDS ${SMOKE_SHADER_DIR}/smoke.vert ${SMOKE_SHADER_DIR}/smoke.frag
            ${SMOKE_SHADER_DIR}/velocity.frag ${SMOKE_SHADER_DIR}/divergence.frag
            ${SMOKE_SHADER_DIR}/pressure.frag ${SMOKE_SHADER_DIR}/project.frag
            ${SMOKE_SHADER_DIR}/advect.frag ${SMOKE_SHADER_DIR}/dye.frag
            ${SMOKE_SHADER_DIR}/common.glsl ${SMOKE_SHADER_EMBEDDER}
    VERBATIM
)
add_custom_target(generate-smoke-shaders DEPENDS ${CONFIG_GENERATED_DIR}/smoke_shaders_generated.hpp)

target_sources(hyprimate-core PRIVATE
    src/window/smoke/Smoke.cpp
    src/window/smoke/SmokeTransit.cpp
)

target_sources(hyprimate PRIVATE
    src/window/smoke/SmokeLiveStyle.cpp
    src/window/smoke/SmokeRenderer.cpp
    src/window/smoke/SmokeMaterial.cpp
)

add_dependencies(hyprimate generate-smoke-shaders)
if(TARGET clang-tidy-check)
    add_dependencies(clang-tidy-check generate-smoke-shaders)
endif()

if(BUILD_TESTING)
    add_executable(smoke-tests src/window/smoke/tests/SmokeTests.cpp)
    target_link_libraries(smoke-tests PRIVATE hyprimate-core)
    target_include_directories(smoke-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME smoke-tests COMMAND smoke-tests)

    add_executable(smoke-material-tests
        src/window/smoke/tests/SmokeMaterialTests.cpp
        src/window/smoke/SmokeMaterial.cpp)
    add_dependencies(smoke-material-tests generate-smoke-shaders)
    target_include_directories(smoke-material-tests PRIVATE ${CONFIG_GENERATED_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(smoke-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME smoke-material-tests COMMAND smoke-material-tests)
    set_tests_properties(smoke-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
endif()
