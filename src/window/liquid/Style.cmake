target_sources(hyprimate-core PRIVATE
    src/window/liquid/EdgeTransition.cpp
    src/window/liquid/LiquidPose.cpp
    src/window/liquid/LiquidMesh.cpp
)

target_sources(hyprimate PRIVATE
    src/window/liquid/LiquidLiveStyle.cpp
)

if(BUILD_TESTING)
    add_executable(liquid-mesh-tests src/window/liquid/tests/LiquidMeshTests.cpp)
    target_link_libraries(liquid-mesh-tests PRIVATE hyprimate-core)
    target_include_directories(liquid-mesh-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME liquid-mesh-tests COMMAND liquid-mesh-tests)

    add_executable(liquid-style-tests
        src/window/liquid/tests/LiquidStyleTests.cpp
        src/window/liquid/LiquidLiveStyle.cpp)
    add_dependencies(liquid-style-tests generate-config)
    target_include_directories(liquid-style-tests PRIVATE
        ${CONFIG_GENERATED_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(liquid-style-tests PRIVATE hyprimate-core)
    add_test(NAME liquid-style-tests COMMAND liquid-style-tests)
endif()
