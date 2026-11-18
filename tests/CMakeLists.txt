if(GLIDE_HAS_REF_SDK AND Catch2_FOUND)
    # --- Glide 2.x Test Suite ---
    add_executable(Glide2xWrapperTests
        test_architecture.cpp
        tlib2_patched.c
        "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c"
    )

    target_include_directories(Glide2xWrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
        "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests"
        "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc"
    )

    target_compile_definitions(Glide2xWrapperTests PRIVATE -D__linux__ -DGLIDE_VERSION=2)

    target_link_libraries(Glide2xWrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        glide2x
        ${CMAKE_DL_LIBS}
        -lm
    )

    # --- Glide 3.x Test Suite ---
    add_executable(Glide3xWrapperTests
        test_glide3.cpp
        test_simd_vec.cpp
        tlib3_patched.c
        "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c"
    )

    set_source_files_properties(test_simd_vec.cpp PROPERTIES COMPILE_FLAGS "-mavx2")

    target_include_directories(Glide3xWrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
        "${GLIDE_REF_SDK_DIR}/glide3x/cvg/glide3/tests"
        "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc"
    )

    target_compile_definitions(Glide3xWrapperTests PRIVATE
        -D__linux__
        -DGLIDE_VERSION=3
        "-DGLIDE_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
    )

    target_link_libraries(Glide3xWrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        glide3x
        ${CMAKE_DL_LIBS}
        -lm
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(Glide2xWrapperTests)
    catch_discover_tests(Glide3xWrapperTests)
elseif(GLIDE_HAS_REF_SDK AND NOT Catch2_FOUND)
    message(STATUS "Catch2 not found; skipping Glide2xWrapperTests and Glide3xWrapperTests")
endif()

option(BUILD_VISUAL_TESTS "Build legacy visual regression test binaries" OFF)

if(BUILD_VISUAL_TESTS)
    add_executable(rle_to_ppm rle_to_ppm.cpp)

    if(GLIDE_HAS_REF_SDK)
        add_executable(Glide2xTest00
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/test00.c"
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/tlib.c"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c"
        )
        target_include_directories(Glide2xTest00 PRIVATE
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
        )
        target_compile_definitions(Glide2xTest00 PRIVATE -D__linux__)
        target_link_libraries(Glide2xTest00 PRIVATE
            glide2x
            ${CMAKE_DL_LIBS}
            -lm
        )

        add_executable(Glide2xTest01
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/test01.c"
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/tlib.c"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c"
        )
        target_include_directories(Glide2xTest01 PRIVATE
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
        )
        target_compile_definitions(Glide2xTest01 PRIVATE -D__linux__)
        target_link_libraries(Glide2xTest01 PRIVATE
            glide2x
            ${CMAKE_DL_LIBS}
            -lm
        )

        add_executable(Glide2xTest02
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/test02.c"
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/tlib.c"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c"
        )
        target_include_directories(Glide2xTest02 PRIVATE
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
        )
        target_compile_definitions(Glide2xTest02 PRIVATE -D__linux__)
        target_link_libraries(Glide2xTest02 PRIVATE
            glide2x
            ${CMAKE_DL_LIBS}
            -lm
        )

        add_executable(text_test
            text_test.cpp
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/tlib.c"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c"
        )
        target_include_directories(text_test PRIVATE
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
        )
        target_compile_definitions(text_test PRIVATE -D__linux__)
        target_link_libraries(text_test PRIVATE
            glide2x
            ${CMAKE_DL_LIBS}
            -lm
        )

        add_executable(test_runner
            framework/TestRunner.cpp
            deprecated/test00_clear/test00.cpp
            deprecated/test01_points/test01.cpp
            deprecated/test02_lines/test02.cpp
            deprecated/test03_shading/test03.cpp
            deprecated/test04_triangles/test04.cpp
            deprecated/test05_zbuffer/test05.cpp
            deprecated/test06_wbuffer/test06.cpp
            deprecated/test07_blend/test07.cpp
            deprecated/test08_fog/test08.cpp
            deprecated/test09_chromakey/test09.cpp
            deprecated/test10_culling/test10.cpp
            deprecated/test11_lfb/test11.cpp
            deprecated/test12_lfb_buffer/test12.cpp
            deprecated/test13_alpha/test13.cpp
            deprecated/test14_depthbias/test14.cpp
            deprecated/test15_clip/test15.cpp
            deprecated/test16_logo/test16.cpp
            deprecated/test17_texture/test17.cpp
            deprecated/test18_texture_alpha/test18.cpp
            deprecated/test19_texture_filter/test19.cpp
            deprecated/test29_stw_hint/test29.cpp
            deprecated/test21_compositing/test21.cpp
            deprecated/test26_writeregion/test26.cpp
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests/tlib.c"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c"
        )
        target_include_directories(test_runner PRIVATE
            "${GLIDE_REF_SDK_DIR}/glide2x/cvg/glide/tests"
            "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
        )
        target_compile_definitions(test_runner PRIVATE -D__linux__)
        target_link_libraries(test_runner PRIVATE
            glide2x
            ${CMAKE_DL_LIBS}
            -lm
        )
    else()
        message(STATUS "Skipping legacy visual regression targets (refs/3dfx-Glide-API not present)")
    endif()
endif()

# --- Headless C++ Benchmark and Validation Tooling ---
add_executable(sw_benchmark sw_benchmark.cpp)
target_include_directories(sw_benchmark PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
)
target_compile_definitions(sw_benchmark PRIVATE -D__linux__ -DGLIDE_VERSION=3)
target_link_libraries(sw_benchmark PRIVATE
    GlideWrapperCore
    glide3x
    ${CMAKE_DL_LIBS}
    -lm
)

add_executable(sw_validate sw_validate.cpp)
target_include_directories(sw_validate PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
)
target_compile_definitions(sw_validate PRIVATE -D__linux__ -DGLIDE_VERSION=3)
target_link_libraries(sw_validate PRIVATE
    GlideWrapperCore
    glide3x
    ${CMAKE_DL_LIBS}
    -lm
)

