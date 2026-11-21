target_sources(hyprimate-core PRIVATE
    src/animations/paper/PaperCrumple.cpp
    src/animations/paper/PaperInteraction.cpp
    src/animations/paper/PaperTopology.cpp
    src/animations/paper/PaperContact.cpp
    src/animations/paper/PaperWorkers.cpp
)

target_sources(hyprimate PRIVATE
    src/animations/paper/PaperLiveStyle.cpp
    src/animations/paper/PaperDraw.cpp
    src/animations/paper/PaperRenderer.cpp
    src/animations/paper/PaperMaterial.cpp
)

if(BUILD_TESTING)
    add_executable(paper-crumple-tests src/animations/paper/tests/PaperCrumpleTests.cpp)
    target_link_libraries(paper-crumple-tests PRIVATE hyprimate-core)
    target_include_directories(paper-crumple-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME paper-crumple-tests COMMAND paper-crumple-tests)

    add_executable(paper-workers-tests src/animations/paper/tests/PaperWorkersTests.cpp)
    target_link_libraries(paper-workers-tests PRIVATE hyprimate-core ${CMAKE_DL_LIBS})
    target_include_directories(paper-workers-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME paper-workers-tests COMMAND paper-workers-tests)
    set_tests_properties(paper-workers-tests PROPERTIES TIMEOUT 45)

    foreach(kind IN ITEMS Topology Contact Interaction)
        string(TOLOWER "${kind}" target)
        add_executable(paper-${target}-tests src/animations/paper/tests/Paper${kind}Tests.cpp)
        target_link_libraries(paper-${target}-tests PRIVATE hyprimate-core)
        target_include_directories(paper-${target}-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME paper-${target}-tests COMMAND paper-${target}-tests)
    endforeach()

    add_executable(paper-material-tests tests/PaperMaterialTests.cpp
        src/animations/paper/PaperMaterial.cpp)
    target_link_libraries(paper-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    target_include_directories(paper-material-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/dev/bench)
    add_test(NAME paper-material-tests COMMAND paper-material-tests)
    set_tests_properties(paper-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")

    add_executable(paper-style-tests
        src/animations/paper/tests/PaperStyleTests.cpp
        src/animations/paper/PaperLiveStyle.cpp)
    add_dependencies(paper-style-tests generate-config)
    target_link_libraries(paper-style-tests PRIVATE hyprimate-core)
    target_include_directories(paper-style-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CONFIG_GENERATED_DIR})
    add_test(NAME paper-style-tests COMMAND paper-style-tests)
endif()
