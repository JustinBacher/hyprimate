set(BUBBLE_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/window/bubble/shaders)
set(BUBBLE_SHADER_EMBEDDER ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.cmake)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp
    COMMAND ${CMAKE_COMMAND}
            -DSTYLE=bubble
            -DSOURCE_DIR=${BUBBLE_SHADER_DIR}
            -DOUTPUT=${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp
            -DDEPFILE=${CONFIG_GENERATED_DIR}/bubble_shaders_generated.d
            -P ${BUBBLE_SHADER_EMBEDDER}
    DEPENDS ${BUBBLE_SHADER_EMBEDDER}
    DEPFILE ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.d
    VERBATIM
)
add_custom_target(generate-bubble-shaders DEPENDS ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp)

target_sources(hyprimate-core PRIVATE
    src/window/bubble/Bubble.cpp
    src/window/bubble/BubbleFrame.cpp
    src/window/bubble/BubbleFoam.cpp
    src/window/bubble/BubbleFormation.cpp
    src/window/bubble/BubbleRupture.cpp
    src/window/bubble/BubbleRuptureEvents.cpp
    src/window/bubble/BubbleSheets.cpp
    src/window/bubble/BubbleDebris.cpp
    src/window/bubble/BubbleOptics.cpp
    src/window/bubble/BubbleMotion.cpp
    src/window/bubble/BubbleDeformation.cpp
    src/window/bubble/BubbleMicrobubbles.cpp
    src/window/bubble/BubbleMotionTrail.cpp
    src/window/bubble/BubbleWake.cpp
)

target_sources(hyprimate PRIVATE
    src/window/bubble/BubbleLiveStyle.cpp
    src/window/bubble/BubbleRenderer.cpp
    src/window/bubble/BubbleMaterial.cpp
    src/window/bubble/BubbleMaterialMembranes.cpp
    src/window/bubble/BubbleMaterialEffects.cpp
    src/window/bubble/BubbleVestige.cpp
    src/window/bubble/BubbleWallpaper.cpp
)

add_dependencies(hyprimate generate-bubble-shaders)
if(TARGET clang-tidy-check)
    add_dependencies(clang-tidy-check generate-bubble-shaders)
endif()

if(BUILD_TESTING)
    add_executable(bubble-tests src/window/bubble/tests/BubbleTests.cpp)
    target_link_libraries(bubble-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-tests COMMAND bubble-tests)

    add_executable(bubble-foam-tests src/window/bubble/tests/BubbleFoamTests.cpp)
    target_link_libraries(bubble-foam-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-foam-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-foam-tests COMMAND bubble-foam-tests)

    add_executable(bubble-formation-tests src/window/bubble/tests/BubbleFormationTests.cpp)
    target_link_libraries(bubble-formation-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-formation-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-formation-tests COMMAND bubble-formation-tests)

    add_executable(bubble-rupture-tests src/window/bubble/tests/BubbleRuptureTests.cpp)
    target_link_libraries(bubble-rupture-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-rupture-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-rupture-tests COMMAND bubble-rupture-tests)

    add_executable(bubble-optics-tests src/window/bubble/tests/BubbleOpticsTests.cpp)
    target_link_libraries(bubble-optics-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-optics-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-optics-tests COMMAND bubble-optics-tests)

    add_executable(bubble-motion-tests src/window/bubble/tests/BubbleMotionTests.cpp)
    target_link_libraries(bubble-motion-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-motion-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-motion-tests COMMAND bubble-motion-tests)

    pkg_check_modules(BUBBLE_TEST_PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
    add_executable(bubble-vestige-tests
        src/window/bubble/tests/BubbleVestigeTests.cpp
        src/window/bubble/BubbleLiveStyle.cpp
        src/window/bubble/BubbleVestige.cpp)
    add_dependencies(bubble-vestige-tests generate-config)
    target_include_directories(bubble-vestige-tests PRIVATE ${CONFIG_GENERATED_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(bubble-vestige-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(bubble-vestige-tests PRIVATE -Wl,--gc-sections)
    target_link_libraries(bubble-vestige-tests PRIVATE hyprimate-core PkgConfig::HYPRLAND PkgConfig::BUBBLE_TEST_PIXMAN)
    add_test(NAME bubble-vestige-tests COMMAND bubble-vestige-tests)

    add_executable(bubble-material-tests
        src/window/bubble/tests/BubbleMaterialTests.cpp
        src/window/bubble/BubbleMaterial.cpp
        src/window/bubble/BubbleMaterialMembranes.cpp
        src/window/bubble/BubbleMaterialEffects.cpp)
    add_dependencies(bubble-material-tests generate-bubble-shaders)
    target_include_directories(bubble-material-tests PRIVATE ${CONFIG_GENERATED_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(bubble-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME bubble-material-tests COMMAND bubble-material-tests)
    set_tests_properties(bubble-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
endif()
