target_sources(hyprimate-core PRIVATE
    src/animations/cloth/ClothFlutter.cpp
)

target_sources(hyprimate PRIVATE
    src/animations/cloth/ClothLiveStyle.cpp
)

set_source_files_properties(src/animations/cloth/ClothFlutter.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")

if(BUILD_TESTING)
    add_executable(cloth-flutter-tests src/animations/cloth/tests/ClothFlutterTests.cpp)
    target_link_libraries(cloth-flutter-tests PRIVATE hyprimate-core Jolt)
    target_include_directories(cloth-flutter-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME cloth-flutter-tests COMMAND cloth-flutter-tests)

    add_executable(cloth-lease-failure-tests src/animations/cloth/tests/ClothLeaseFailureTests.cpp)
    target_link_libraries(cloth-lease-failure-tests PRIVATE hyprimate-core Jolt)
    add_test(NAME cloth-lease-failure-tests COMMAND cloth-lease-failure-tests)
endif()
