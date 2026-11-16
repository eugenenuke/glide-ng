if(GLIDE_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror)
endif()

# --- Self-Contained Test Library Compatibility Layer (tlib_compat) ---
add_library(tlib_compat STATIC 
    tlib_compat.cpp
)
target_include_directories(tlib_compat PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# --- Shared Diagnostic Helper for Glide 2.x ---
add_library(tool_info2 STATIC DiagnosticInfo.cpp)
target_compile_definitions(tool_info2 PRIVATE GLIDE_VERSION=2)
target_link_libraries(tool_info2 PUBLIC ${CMAKE_DL_LIBS})
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

# --- Glide 2.x System Info Tool (Unified Pure C99) ---
add_executable(glide2_info glide_info/glide_info.c)
set_target_properties(glide2_info PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_link_libraries(glide2_info PRIVATE glide2x ${CMAKE_DL_LIBS})
target_compile_definitions(glide2_info PRIVATE GLIDE_VERSION=2)
target_include_directories(glide2_info PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)

# --- Glide 3.x System Info Tool (Unified Pure C99) ---
add_executable(glide3_info glide_info/glide_info.c)
set_target_properties(glide3_info PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_link_libraries(glide3_info PRIVATE glide3x ${CMAKE_DL_LIBS})
target_compile_definitions(glide3_info PRIVATE GLIDE_VERSION=3)
target_include_directories(glide3_info PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)

# --- Optional DOS/32 CauseWay glide_info.exe (Open Watcom v2) ---
add_dos_glide_executable(glide_info
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/glide_info/glide_info.c"
    INCLUDES
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/include/glide2"
        "${CMAKE_SOURCE_DIR}/include/3dfx"
        "${CMAKE_CURRENT_SOURCE_DIR}/common"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/include/glide_ng/env_vars.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/glide_tool_platform.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/glide2_dos_ovl.h"
)

# --- Glide 2.x Benchmark Suite (Unified Pure C99) ---
set(GLIDE_BENCH_SOURCES
    glide_bench/glide_bench.c
    glide_bench/bench_hud.c
    glide_bench/bench_scenes.c
)

add_executable(glide2_bench ${GLIDE_BENCH_SOURCES})
set_target_properties(glide2_bench PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_link_libraries(glide2_bench PRIVATE glide2x m ${CMAKE_DL_LIBS})
target_compile_definitions(glide2_bench PRIVATE GLIDE_VERSION=2)
target_include_directories(glide2_bench PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench"
)

# --- Glide 3.x Benchmark Suite (Unified Pure C99) ---
add_executable(glide3_bench ${GLIDE_BENCH_SOURCES})
set_target_properties(glide3_bench PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_link_libraries(glide3_bench PRIVATE glide3x m ${CMAKE_DL_LIBS})
target_compile_definitions(glide3_bench PRIVATE GLIDE_VERSION=3)
target_include_directories(glide3_bench PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench"
)

# --- Optional DOS/32 CauseWay glide_bench.exe (Open Watcom v2) ---
add_dos_glide_executable(glide_bench
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench/glide_bench.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench/bench_hud.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench/bench_scenes.c"
    INCLUDES
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/include/glide2"
        "${CMAKE_SOURCE_DIR}/include/3dfx"
        "${CMAKE_CURRENT_SOURCE_DIR}/common"
        "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/common/glide_tool_platform.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/glide2_dos_ovl.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench/glide_bench.h"
)



# add_subdirectory(snake)
