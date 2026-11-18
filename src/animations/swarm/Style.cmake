set(SWARM_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/animations/swarm/shaders)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/swarm_shaders_generated.hpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
            --source-dir ${SWARM_SHADER_DIR}
            --output ${CONFIG_GENERATED_DIR}/swarm_shaders_generated.hpp
            --namespace swarm_shader --shader SURFACE_VERTEX=surface.vert --shader SURFACE_FRAGMENT=surface.frag
            --shader FIELD_VERTEX=field.vert --shader FIELD_FRAGMENT=field.frag
    DEPENDS ${SWARM_SHADER_DIR}/surface.vert ${SWARM_SHADER_DIR}/surface.frag
            ${SWARM_SHADER_DIR}/field.vert ${SWARM_SHADER_DIR}/field.frag
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
    VERBATIM
)
add_custom_target(generate-swarm-shaders DEPENDS ${CONFIG_GENERATED_DIR}/swarm_shaders_generated.hpp)

target_sources(hyprimate-core PRIVATE
    src/animations/swarm/Swarm.cpp
    src/animations/swarm/SwarmSprite.cpp
)

target_sources(hyprimate PRIVATE
    src/animations/swarm/SwarmLiveStyle.cpp
    src/animations/swarm/SwarmRenderer.cpp
    src/animations/swarm/SwarmSurface.cpp
)

add_dependencies(hyprimate generate-swarm-shaders)
add_dependencies(clang-tidy-check generate-swarm-shaders)

target_compile_definitions(hyprimate PRIVATE HYPRIMATE_SPRITE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/animations/swarm/sprites")

if(BUILD_TESTING)
    add_executable(swarm-tests src/animations/swarm/tests/SwarmTests.cpp)
    target_link_libraries(swarm-tests PRIVATE hyprimate-core)
    target_include_directories(swarm-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME swarm-tests COMMAND swarm-tests)
endif()
