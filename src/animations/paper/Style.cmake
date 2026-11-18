target_sources(hyprimate-core PRIVATE
    src/animations/paper/PaperCrumple.cpp
    src/animations/paper/PaperTakes.cpp
)

target_sources(hyprimate PRIVATE
    src/animations/paper/PaperLiveStyle.cpp
)

# PaperTakes.cpp #embeds the baked crumples; rebuild it whenever they are re-packed.
set_source_files_properties(src/animations/paper/PaperTakes.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/animations/paper/assets/paper_takes.bin)

if(BUILD_TESTING)
    add_executable(paper-crumple-tests src/animations/paper/tests/PaperCrumpleTests.cpp)
    target_link_libraries(paper-crumple-tests PRIVATE hyprimate-core)
    target_include_directories(paper-crumple-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME paper-crumple-tests COMMAND paper-crumple-tests)
endif()
