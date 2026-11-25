target_sources(hyprimate-core PRIVATE
    src/window/paper/PaperCrumple.cpp
    src/window/paper/PaperInteraction.cpp
    src/window/paper/PaperTopology.cpp
    src/window/paper/PaperContact.cpp
    src/window/paper/PaperWorkers.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(hyprimate-core PRIVATE Threads::Threads)

target_sources(hyprimate PRIVATE
    src/window/paper/PaperLiveStyle.cpp
    src/window/paper/PaperDraw.cpp
    src/window/paper/PaperRenderer.cpp
    src/window/paper/PaperMaterial.cpp
    src/window/paper/PaperRenderMesh.cpp
)

if(BUILD_TESTING)
    add_executable(paper-crumple-tests src/window/paper/tests/PaperCrumpleTests.cpp)
    target_link_libraries(paper-crumple-tests PRIVATE hyprimate-core)
    target_include_directories(paper-crumple-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME paper-crumple-tests COMMAND paper-crumple-tests)

    add_executable(paper-workers-tests src/window/paper/tests/PaperWorkersTests.cpp)
    target_link_libraries(paper-workers-tests PRIVATE hyprimate-core ${CMAKE_DL_LIBS})
    target_include_directories(paper-workers-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME paper-workers-tests COMMAND paper-workers-tests)
    set_tests_properties(paper-workers-tests PROPERTIES TIMEOUT 45)

    foreach(kind IN ITEMS Topology Contact Interaction)
        string(TOLOWER "${kind}" target)
        add_executable(paper-${target}-tests src/window/paper/tests/Paper${kind}Tests.cpp)
        target_link_libraries(paper-${target}-tests PRIVATE hyprimate-core)
        target_include_directories(paper-${target}-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME paper-${target}-tests COMMAND paper-${target}-tests)
    endforeach()

    add_executable(paper-material-tests tests/PaperMaterialTests.cpp
        src/window/paper/PaperMaterial.cpp
        src/window/paper/PaperRenderMesh.cpp)
    target_link_libraries(paper-material-tests PRIVATE hyprimate-core PkgConfig::TEST_EGL)
    target_include_directories(paper-material-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/dev/bench)
    add_test(NAME paper-material-tests COMMAND paper-material-tests)
    set_tests_properties(paper-material-tests PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless")

    add_executable(paper-render-mesh-failure-tests
        src/window/paper/tests/PaperRenderMeshFailureTests.cpp
        src/window/paper/PaperRenderMesh.cpp)
    target_link_libraries(paper-render-mesh-failure-tests PRIVATE hyprimate-core)
    target_include_directories(paper-render-mesh-failure-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME paper-render-mesh-failure-tests COMMAND paper-render-mesh-failure-tests)

    add_executable(paper-style-tests
        src/window/paper/tests/PaperStyleTests.cpp
        src/window/paper/PaperLiveStyle.cpp)
    add_dependencies(paper-style-tests generate-config)
    target_link_libraries(paper-style-tests PRIVATE hyprimate-core)
    target_include_directories(paper-style-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CONFIG_GENERATED_DIR})
    add_test(NAME paper-style-tests COMMAND paper-style-tests)
endif()
