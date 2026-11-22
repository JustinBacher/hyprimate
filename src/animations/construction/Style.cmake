set(construction_dir ${CMAKE_CURRENT_LIST_DIR})
set(construction_png ${construction_dir}/art/construction.png)
set(construction_embed ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_construction_atlas.py)
set(construction_header ${CONFIG_GENERATED_DIR}/construction_atlas_generated.hpp)
add_custom_command(OUTPUT ${construction_header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CONFIG_GENERATED_DIR}
    COMMAND ${Python3_EXECUTABLE} ${construction_embed} --input ${construction_png} --output ${construction_header}
    DEPENDS ${construction_embed} ${construction_png}
    VERBATIM)
find_package(PNG REQUIRED)

target_sources(hyprimate-core PRIVATE
    ${construction_dir}/ConstructionSite.cpp
    ${construction_dir}/ConstructionCrew.cpp
    ${construction_dir}/studies/IntactTransport.cpp
    ${construction_dir}/studies/EdgeStretch.cpp
    ${construction_dir}/ConstructionMotionArt.cpp
    ${construction_dir}/ConstructionMotionSite.cpp)
target_sources(hyprimate PRIVATE
    ${construction_dir}/ConstructionLiveStyle.cpp
    ${construction_dir}/ConstructionRenderer.cpp
    ${construction_dir}/ConstructionMaterial.cpp
    ${construction_header})
target_link_libraries(hyprimate PRIVATE PNG::PNG)

if(BUILD_TESTING)
    function(add_construction_test target test_source)
        add_executable(${target} ${construction_dir}/${test_source} ${ARGN})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(${target} PRIVATE hyprimate-core)
        add_test(NAME ${target} COMMAND ${target})
    endfunction()
    add_construction_test(construction-site-tests tests/ConstructionSiteTests.cpp)
    add_construction_test(construction-motion-art-tests tests/ConstructionMotionArtTests.cpp)
    add_construction_test(construction-motion-site-tests tests/ConstructionMotionSiteTests.cpp)
    add_construction_test(construction-intact-transport-tests studies/tests/IntactTransportTests.cpp)
    add_construction_test(construction-edge-stretch-tests studies/tests/EdgeStretchTests.cpp)
    # These models remain test-only; neither can acquire a live window.
    add_construction_test(construction-piece-relocation-tests studies/tests/PieceRelocationTests.cpp
        ${construction_dir}/studies/PieceRelocation.cpp)
    add_construction_test(construction-regional-close-tests studies/tests/RegionalCloseTests.cpp
        ${construction_dir}/studies/RegionalClose.cpp)
    add_executable(construction-material-tests
        ${construction_dir}/tests/ConstructionMaterialTests.cpp
        ${construction_dir}/ConstructionMaterial.cpp ${construction_header})
    target_include_directories(construction-material-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CONFIG_GENERATED_DIR})
    target_link_libraries(construction-material-tests PRIVATE PkgConfig::TEST_EGL PNG::PNG)
    add_test(NAME construction-material-tests COMMAND construction-material-tests)
    add_test(NAME construction-material-es30-tests COMMAND construction-material-tests --require-es30)
    set_tests_properties(construction-material-tests construction-material-es30-tests
        PROPERTIES ENVIRONMENT "EGL_PLATFORM=surfaceless" SKIP_RETURN_CODE 77)
    set_property(TEST construction-material-es30-tests APPEND PROPERTY ENVIRONMENT MESA_GLES_VERSION_OVERRIDE=3.0)
endif()
