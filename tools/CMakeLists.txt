# --- Self-Contained Test Library Compatibility Layer (tlib_compat) ---
add_library(tlib_compat STATIC 
    tlib_compat.cpp
)
target_include_directories(tlib_compat PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_subdirectory(glidegears)
add_subdirectory(glidecube)
