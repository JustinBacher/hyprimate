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
    src/window/swarm/SwarmStep.cpp
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

# At the old default directory, existing files win and missing built-ins fall
# back to embedded data. This also applies to an explicit choice of that path.
set(swarm_default_sprite_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/window/swarm/sprites")
set(HYPRIMATE_SPRITE_DIR "${swarm_default_sprite_dir}" CACHE PATH "Directory containing Swarm sprites")
if(HYPRIMATE_SPRITE_DIR STREQUAL swarm_default_sprite_dir)
    set(swarm_embed_builtin 1)
else()
    set(swarm_embed_builtin 0)
endif()
target_compile_definitions(hyprimate PRIVATE HYPRIMATE_SPRITE_DIR="${HYPRIMATE_SPRITE_DIR}"
    HYPRIMATE_EMBED_BUILTIN_SPRITES=${swarm_embed_builtin})
set(swarm_sprite_files
    "${swarm_default_sprite_dir}/bird.png"
    "${swarm_default_sprite_dir}/cat.png"
    "${swarm_default_sprite_dir}/spider.png")
set(swarm_sprite_source "${CONFIG_GENERATED_DIR}/swarm_sprites_generated.cpp")
set(swarm_sprite_embedder "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_swarm_sprites.cmake")
add_custom_command(OUTPUT "${swarm_sprite_source}"
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${swarm_default_sprite_dir}"
            "-DOUTPUT=${swarm_sprite_source}" -P "${swarm_sprite_embedder}"
    DEPENDS ${swarm_sprite_files} "${swarm_sprite_embedder}"
    VERBATIM)
add_custom_target(generate-swarm-sprites DEPENDS "${swarm_sprite_source}")
pkg_check_modules(SWARM_CAIRO REQUIRED IMPORTED_TARGET cairo)
add_library(swarm-sprite-assets STATIC "${swarm_sprite_source}" src/window/swarm/SwarmSpriteLoader.cpp)
add_dependencies(swarm-sprite-assets generate-swarm-sprites)
target_include_directories(swarm-sprite-assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(swarm-sprite-assets PUBLIC PkgConfig::SWARM_CAIRO PRIVATE hyprimate-core)
target_link_libraries(hyprimate PRIVATE swarm-sprite-assets)
if(TARGET clang-tidy-check)
    add_dependencies(clang-tidy-check generate-swarm-sprites)
endif()

if(BUILD_TESTING)
    add_executable(swarm-tests src/window/swarm/tests/SwarmTests.cpp)
    target_link_libraries(swarm-tests PRIVATE hyprimate-core)
    target_include_directories(swarm-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME swarm-tests COMMAND swarm-tests)

    add_executable(swarm-sprite-assets-tests src/window/swarm/tests/SwarmSpriteAssetsTests.cpp)
    target_link_libraries(swarm-sprite-assets-tests PRIVATE swarm-sprite-assets hyprimate-core)
    target_include_directories(swarm-sprite-assets-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(swarm-sprite-assets-tests PRIVATE SWARM_TEST_SPRITE_DIR="${swarm_default_sprite_dir}")
    add_test(NAME swarm-sprite-assets-tests COMMAND swarm-sprite-assets-tests)
endif()
