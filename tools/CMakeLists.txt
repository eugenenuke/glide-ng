# --- Glide 2 Test Library (tlib2) ---
add_library(tlib2 STATIC 
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/glide2x/cvg/glide/tests/tlib.c"
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)
target_compile_definitions(tlib2 PRIVATE GLIDE_VERSION=2)
target_include_directories(tlib2 PUBLIC 
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/glide2x/cvg/glide/tests"
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/swlibs/fxmisc"
)

# --- Glide 3 Test Library (tlib3) ---
add_library(tlib3 STATIC 
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/glide3x/cvg/glide3/tests/tlib.c"
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/swlibs/fxmisc/linutil.c"
)
target_compile_definitions(tlib3 PRIVATE GLIDE_VERSION=3)
target_include_directories(tlib3 PUBLIC 
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/glide3x/cvg/glide3/tests"
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/swlibs/fxmisc"
)

add_subdirectory(glidegears)
add_subdirectory(glidecube)
