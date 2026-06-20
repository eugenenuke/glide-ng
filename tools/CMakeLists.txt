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

# --- Shared Diagnostic Helper for Glide 2.x ---
add_library(tool_info2 STATIC DiagnosticInfo.cpp)
target_compile_definitions(tool_info2 PRIVATE GLIDE_VERSION=2)
target_include_directories(tool_info2 PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}" 
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
)

# --- Shared Diagnostic Helper for Glide 3.x ---
add_library(tool_info3 STATIC DiagnosticInfo.cpp)
target_compile_definitions(tool_info3 PRIVATE GLIDE_VERSION=3)
target_include_directories(tool_info3 PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}" 
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
)

add_subdirectory(glidegears)
add_subdirectory(glidecube)

# --- Glide 2.x System Info Tool ---
add_executable(glide2_info glide_info.cpp)
target_link_libraries(glide2_info PRIVATE glide2x tool_info2)
target_compile_definitions(glide2_info PRIVATE GLIDE_VERSION=2)
target_include_directories(glide2_info PRIVATE 
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
)

# --- Glide 3.x System Info Tool ---
add_executable(glide3_info glide_info.cpp)
target_link_libraries(glide3_info PRIVATE glide3x tool_info3)
target_compile_definitions(glide3_info PRIVATE GLIDE_VERSION=3)
target_include_directories(glide3_info PRIVATE 
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
)


