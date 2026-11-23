# =====================================================================
# glide-ng: Interactive Showcase Demos CMake Configuration
# =====================================================================

if(GLIDE_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror)
endif()

# --- Glide 2.x Interactive Fog Showcase ---
add_executable(glide2_fog glide2_fog.cpp)
target_include_directories(glide2_fog PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools"
)
target_compile_definitions(glide2_fog PRIVATE -D__linux__)
target_link_libraries(glide2_fog PRIVATE
    glide2x
    tool_info2
    tlib_compat
    ${CMAKE_DL_LIBS}
    -lm
)

# --- Glide 2.x Interactive Gamma / RAMDAC LUT Showcase ---
add_executable(glide2_gamma glide2_gamma.cpp)
target_include_directories(glide2_gamma PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools"
)
target_compile_definitions(glide2_gamma PRIVATE -D__linux__)
target_link_libraries(glide2_gamma PRIVATE
    glide2x
    tool_info2
    tlib_compat
    ${CMAKE_DL_LIBS}
    -lm
)

# --- Glide 2.x Interactive Perspective Mipmapping Showcase ---
add_executable(glide2_mipmap glide2_mipmap.cpp)
target_include_directories(glide2_mipmap PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools"
)
target_compile_definitions(glide2_mipmap PRIVATE -D__linux__)
target_link_libraries(glide2_mipmap PRIVATE
    glide2x
    tool_info2
    tlib_compat
    ${CMAKE_DL_LIBS}
    -lm
)

# --- Glide 3.x Interactive State & Clip-Coord Showcase ---
add_executable(glide3_state glide3_state.cpp)
target_include_directories(glide3_state PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools"
)
target_compile_definitions(glide3_state PRIVATE -D__linux__ -DGLIDE_VERSION=3)
target_link_libraries(glide3_state PRIVATE
    glide3x
    tool_info3
    tlib_compat
    ${CMAKE_DL_LIBS}
    -lm
)

# --- Glide 3.x Interactive Extensions Showcase (TEXMIRROR / CHROMA_RANGE / PALETTE6666) ---
add_executable(glide3_extensions glide3_extensions.cpp)
target_include_directories(glide3_extensions PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools"
)
target_compile_definitions(glide3_extensions PRIVATE -D__linux__ -DGLIDE_VERSION=3)
target_link_libraries(glide3_extensions PRIVATE
    glide3x
    tool_info3
    tlib_compat
    ${CMAKE_DL_LIBS}
    -lm
)
