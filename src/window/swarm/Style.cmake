set(SWARM_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/window/swarm/shaders)
set(SWARM_SHADER_EMBEDDER ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.cmake)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/swarm_shaders_generated.hpp
    COMMAND ${CMAKE_COMMAND}
            -DSTYLE=swarm
            -DSOURCE_DIR=${SWARM_SHADER_DIR}
            -DOUTPUT=${CONFIG_GENERATED_DIR}/swarm_shaders_generated.hpp
            -P ${SWARM_SHADER_EMBEDDER}
    DEPENDS ${SWARM_SHADER_DIR}/surface.vert ${SWARM_SHADER_DIR}/surface.frag
            ${SWARM_SHADER_DIR}/field.vert ${SWARM_SHADER_DIR}/field.frag
            ${SWARM_SHADER_EMBEDDER}
    VERBATIM
)
add_custom_target(generate-swarm-shaders DEPENDS ${CONFIG_GENERATED_DIR}/swarm_shaders_generated.hpp)

target_sources(hyprimate-core PRIVATE
    src/window/swarm/Swarm.cpp
    src/window/swarm/SwarmSprite.cpp
)

target_sources(hyprimate PRIVATE
    src/window/swarm/SwarmLiveStyle.cpp
    src/window/swarm/SwarmRenderer.cpp
    src/window/swarm/SwarmSurface.cpp
)

add_dependencies(hyprimate generate-swarm-shaders)
if(TARGET clang-tidy-check)
    add_dependencies(clang-tidy-check generate-swarm-shaders)
endif()

set(HYPRIMATE_SPRITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/window/swarm/sprites" CACHE PATH "Directory containing the built-in Swarm sprites")
target_compile_definitions(hyprimate PRIVATE HYPRIMATE_SPRITE_DIR="${HYPRIMATE_SPRITE_DIR}")

if(BUILD_TESTING)
    add_executable(swarm-tests src/window/swarm/tests/SwarmTests.cpp)
    target_link_libraries(swarm-tests PRIVATE hyprimate-core)
    target_include_directories(swarm-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME swarm-tests COMMAND swarm-tests)
endif()
