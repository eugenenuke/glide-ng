add_executable(GlideWrapperTests
    test_architecture.cpp
)

target_include_directories(GlideWrapperTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
)
target_link_libraries(GlideWrapperTests PRIVATE
    GlideWrapperCore
    Catch2::Catch2WithMain
    ${CMAKE_DL_LIBS}
)

add_executable(rle_to_ppm rle_to_ppm.cpp)

include(CTest)
include(Catch)
catch_discover_tests(GlideWrapperTests)

# add_executable(Glide3xTest00
#     "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide3x/h5/glide3/tests/test00.c"
#     "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide3x/h5/glide3/tests/tlib.c"
#     "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
# )
# target_include_directories(Glide3xTest00 PRIVATE
#     "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide3x/h5/glide3/tests"
#     "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc"
#     "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
#     "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
# )
# target_compile_definitions(Glide3xTest00 PRIVATE -D__linux__)
# target_link_libraries(Glide3xTest00 PRIVATE
#     glide3x
#     ${CMAKE_DL_LIBS}
#     -lm
# )

add_executable(Glide2xTest00
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/test00.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/tlib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)
target_include_directories(Glide2xTest00 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/test01.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/tlib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)
target_include_directories(Glide2xTest01 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/test02.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/tlib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)
target_include_directories(Glide2xTest02 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/tlib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)
target_include_directories(text_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc"
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
    test00_clear/test00.cpp
    test01_points/test01.cpp
    test02_lines/test02.cpp
    test03_shading/test03.cpp
    test04_triangles/test04.cpp
    test05_zbuffer/test05.cpp
    test06_wbuffer/test06.cpp
    test07_blend/test07.cpp
    test08_fog/test08.cpp
    test10_culling/test10.cpp
    test13_alpha/test13.cpp
    test14_depthbias/test14.cpp
    test15_clip/test15.cpp
    test17_texture/test17.cpp
    test18_texture_alpha/test18.cpp
    test19_texture_filter/test19.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/tlib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)
target_include_directories(test_runner PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
)
target_compile_definitions(test_runner PRIVATE -D__linux__)
target_link_libraries(test_runner PRIVATE
    glide2x
    ${CMAKE_DL_LIBS}
    -lm
)
