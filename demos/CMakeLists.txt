add_executable(chromakey_range_demo
    chromakey_range_demo.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests/tlib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)

target_include_directories(chromakey_range_demo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/glide2x/cvg/glide/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/../refs/3dfx-Glide-API/swlibs/fxmisc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools"
)

target_compile_definitions(chromakey_range_demo PRIVATE -D__linux__)

target_link_libraries(chromakey_range_demo PRIVATE
    glide2x
    tool_info2
    ${CMAKE_DL_LIBS}
    -lm
)
