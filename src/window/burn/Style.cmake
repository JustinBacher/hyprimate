target_sources(hyprimate-core PRIVATE
    src/window/burn/BurnDynamics.cpp
    src/window/burn/BurnGround.cpp
)

target_sources(hyprimate PRIVATE
    src/window/burn/BurnLiveStyle.cpp
    src/window/burn/BurnDraw.cpp
    src/window/burn/BurnMaterial.cpp
    src/window/burn/BurnRenderer.cpp
    src/window/burn/BurnGroundDraw.cpp
    src/window/burn/BurnGroundDebris.cpp
    src/window/burn/BurnScreen.cpp
)

if(BUILD_TESTING)
    foreach(kind IN ITEMS Dynamics Ground)
        string(TOLOWER "${kind}" target)
        add_executable(burn-${target}-tests src/window/burn/tests/Burn${kind}Tests.cpp)
        target_include_directories(burn-${target}-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(burn-${target}-tests PRIVATE hyprimate-core)
        add_test(NAME burn-${target}-tests COMMAND burn-${target}-tests)
    endforeach()

    add_executable(burn-material-tests
        src/window/burn/tests/BurnMaterialTests.cpp
        src/window/burn/BurnGroundDebris.cpp
        src/window/burn/BurnMaterial.cpp)
    target_include_directories(burn-material-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(burn-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME burn-material-tests COMMAND burn-material-tests)
    set_tests_properties(burn-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
endif()
