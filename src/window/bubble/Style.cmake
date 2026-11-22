set(BUBBLE_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/window/bubble/shaders)
set(BUBBLE_SHADER_FILES
    ${BUBBLE_SHADER_DIR}/membrane.vert
    ${BUBBLE_SHADER_DIR}/membrane.frag
    ${BUBBLE_SHADER_DIR}/curl.vert
    ${BUBBLE_SHADER_DIR}/curl.frag
    ${BUBBLE_SHADER_DIR}/particle.vert
    ${BUBBLE_SHADER_DIR}/particle.frag
    ${BUBBLE_SHADER_DIR}/membrane.glsl
    ${BUBBLE_SHADER_DIR}/soap.glsl
    ${BUBBLE_SHADER_DIR}/film.vert
    ${BUBBLE_SHADER_DIR}/film.frag
    ${BUBBLE_SHADER_DIR}/microbubble.vert
    ${BUBBLE_SHADER_DIR}/microbubble.frag
)
add_custom_command(
    OUTPUT ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
            --source-dir ${BUBBLE_SHADER_DIR}
            --output ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp
    DEPENDS ${BUBBLE_SHADER_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bubble_shaders.py
    VERBATIM
)
add_custom_target(generate-bubble-shaders DEPENDS ${CONFIG_GENERATED_DIR}/bubble_shaders_generated.hpp)

target_sources(hyprimate-core PRIVATE
    src/window/bubble/Bubble.cpp
    src/window/bubble/BubbleFoam.cpp
    src/window/bubble/BubbleFormation.cpp
    src/window/bubble/BubbleRupture.cpp
    src/window/bubble/BubbleOptics.cpp
    src/window/bubble/BubbleMotion.cpp
    src/window/bubble/BubbleWake.cpp
)

target_sources(hyprimate PRIVATE
    src/window/bubble/BubbleLiveStyle.cpp
    src/window/bubble/BubbleRenderer.cpp
    src/window/bubble/BubbleMaterial.cpp
    src/window/bubble/BubbleVestige.cpp
    src/window/bubble/BubbleWallpaper.cpp
)

add_dependencies(hyprimate generate-bubble-shaders)
add_dependencies(clang-tidy-check generate-bubble-shaders)

if(BUILD_TESTING)
    add_executable(bubble-tests src/window/bubble/tests/BubbleTests.cpp)
    target_link_libraries(bubble-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-tests COMMAND bubble-tests)

    add_executable(bubble-optics-tests src/window/bubble/tests/BubbleOpticsTests.cpp)
    target_link_libraries(bubble-optics-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-optics-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-optics-tests COMMAND bubble-optics-tests)

    add_executable(bubble-motion-tests src/window/bubble/tests/BubbleMotionTests.cpp)
    target_link_libraries(bubble-motion-tests PRIVATE hyprimate-core)
    target_include_directories(bubble-motion-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME bubble-motion-tests COMMAND bubble-motion-tests)

    add_executable(bubble-material-tests
        src/window/bubble/tests/BubbleMaterialTests.cpp
        src/window/bubble/BubbleMaterial.cpp)
    add_dependencies(bubble-material-tests generate-bubble-shaders)
    target_include_directories(bubble-material-tests PRIVATE ${CONFIG_GENERATED_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(bubble-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME bubble-material-tests COMMAND bubble-material-tests)
    set_tests_properties(bubble-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
endif()
