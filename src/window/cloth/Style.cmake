target_sources(hyprimate-core PRIVATE
    src/window/cloth/ClothFlutter.cpp
    src/window/cloth/ClothSolver.cpp
)

target_sources(hyprimate PRIVATE
    src/window/cloth/ClothLiveStyle.cpp
)

set_source_files_properties(src/window/cloth/ClothFlutter.cpp src/window/cloth/ClothSolver.cpp
    PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden;-ffp-contract=off")

find_package(Threads REQUIRED)
target_link_libraries(hyprimate-core PRIVATE Threads::Threads)
if(BUILD_TESTING)
    add_executable(cloth-flutter-tests src/window/cloth/tests/ClothFlutterTests.cpp)
    target_link_libraries(cloth-flutter-tests PRIVATE hyprimate-core Threads::Threads)
    target_include_directories(cloth-flutter-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME cloth-flutter-tests COMMAND cloth-flutter-tests)

    add_executable(cloth-lease-failure-tests src/window/cloth/tests/ClothLeaseFailureTests.cpp)
    target_link_libraries(cloth-lease-failure-tests PRIVATE hyprimate-core Threads::Threads ${CMAKE_DL_LIBS})
    target_link_options(cloth-lease-failure-tests PRIVATE "-Wl,--wrap=pthread_mutex_lock")
    add_test(NAME cloth-lease-failure-tests COMMAND cloth-lease-failure-tests)

    add_executable(cloth-solver-tests src/window/cloth/tests/ClothSolverTests.cpp)
    target_link_libraries(cloth-solver-tests PRIVATE hyprimate-core)
    target_include_directories(cloth-solver-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME cloth-solver-tests COMMAND cloth-solver-tests)

    add_executable(cloth-replay EXCLUDE_FROM_ALL src/window/cloth/tests/ClothReplay.cpp)
    target_link_libraries(cloth-replay PRIVATE hyprimate-core)
endif()
