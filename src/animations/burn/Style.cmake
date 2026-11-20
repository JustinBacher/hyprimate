target_sources(hyprimate-core PRIVATE
    src/animations/burn/BurnDynamics.cpp
    src/animations/burn/BurnGround.cpp
)

target_sources(hyprimate PRIVATE
    src/animations/burn/BurnLiveStyle.cpp
    src/animations/burn/BurnDraw.cpp
    src/animations/burn/BurnMaterial.cpp
    src/animations/burn/BurnRenderer.cpp
    src/animations/burn/BurnGroundDraw.cpp
    src/animations/burn/BurnScreen.cpp
)

if(BUILD_TESTING)
    foreach(kind IN ITEMS Dynamics Ground)
        string(TOLOWER "${kind}" target)
        add_executable(burn-${target}-tests src/animations/burn/tests/Burn${kind}Tests.cpp)
        target_include_directories(burn-${target}-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(burn-${target}-tests PRIVATE hyprimate-core)
        add_test(NAME burn-${target}-tests COMMAND burn-${target}-tests)
    endforeach()

    add_executable(burn-material-tests
        src/animations/burn/tests/BurnMaterialTests.cpp
        src/animations/burn/BurnMaterial.cpp)
    target_include_directories(burn-material-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(burn-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    add_test(NAME burn-material-tests COMMAND burn-material-tests)
    set_tests_properties(burn-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")
endif()
