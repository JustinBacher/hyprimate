target_sources(hyprimate-core PRIVATE
    src/animations/liquid/LiquidPose.cpp
    src/animations/liquid/LiquidMesh.cpp
)

target_sources(hyprimate PRIVATE
    src/animations/liquid/LiquidLiveStyle.cpp
    src/animations/liquid/LiquidCloseStyle.cpp
)

if(BUILD_TESTING)
    add_executable(liquid-mesh-tests src/animations/liquid/tests/LiquidMeshTests.cpp)
    target_link_libraries(liquid-mesh-tests PRIVATE hyprimate-core)
    target_include_directories(liquid-mesh-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME liquid-mesh-tests COMMAND liquid-mesh-tests)
endif()
