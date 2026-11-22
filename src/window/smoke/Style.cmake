set(SMOKE_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/window/smoke/shaders)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/smoke_shaders_generated.hpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
            --source-dir ${SMOKE_SHADER_DIR}
            --output ${CONFIG_GENERATED_DIR}/smoke_shaders_generated.hpp
            --namespace smoke_shader --shader VERTEX=smoke.vert --shader COMPOSITE=smoke.frag
            --shader VELOCITY=velocity.frag --shader DIVERGENCE=divergence.frag
            --shader PRESSURE=pressure.frag --shader PROJECT=project.frag
            --shader ADVECT=advect.frag --shader DYE=dye.frag
    DEPENDS ${SMOKE_SHADER_DIR}/smoke.vert ${SMOKE_SHADER_DIR}/smoke.frag
            ${SMOKE_SHADER_DIR}/velocity.frag ${SMOKE_SHADER_DIR}/divergence.frag
            ${SMOKE_SHADER_DIR}/pressure.frag ${SMOKE_SHADER_DIR}/project.frag
            ${SMOKE_SHADER_DIR}/advect.frag
            ${SMOKE_SHADER_DIR}/dye.frag ${SMOKE_SHADER_DIR}/common.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
    VERBATIM
)
add_custom_target(generate-smoke-shaders DEPENDS ${CONFIG_GENERATED_DIR}/smoke_shaders_generated.hpp)

target_sources(hyprimate-core PRIVATE
    src/window/smoke/Smoke.cpp
)

target_sources(hyprimate PRIVATE
    src/window/smoke/SmokeLiveStyle.cpp
    src/window/smoke/SmokeRenderer.cpp
    src/window/smoke/SmokeMaterial.cpp
)

add_dependencies(hyprimate generate-smoke-shaders)
add_dependencies(clang-tidy-check generate-smoke-shaders)

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
