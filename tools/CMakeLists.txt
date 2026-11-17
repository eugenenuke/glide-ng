if(GLIDE_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror)
endif()

if(NOT WIN32)
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
endif()

# Helper macro to configure portable C99 tools across Linux & Win32
function(configure_glide_c99_tool TARGET_NAME GLIDE_VER NEED_MATH)
    set_target_properties(${TARGET_NAME} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    target_compile_definitions(${TARGET_NAME} PRIVATE GLIDE_VERSION=${GLIDE_VER})
    if(WIN32)
        if(GLIDE_VER EQUAL 2)
            # Win32 Glide 2 tools dynamically load glide2x.dll via LoadLibraryA
            add_dependencies(${TARGET_NAME} glide2x)
        else()
            # Win32 Glide 3 tools link against canonical 3dfx _Func@N import lib
            if(MINGW AND TARGET glide3x_3dfx_implib)
                target_link_libraries(${TARGET_NAME} PRIVATE glide3x_3dfx_implib)
            else()
                target_link_libraries(${TARGET_NAME} PRIVATE glide3x)
            endif()
        endif()
        if(MINGW)
            target_link_options(${TARGET_NAME} PRIVATE -static -mstackrealign)
        endif()
    else()
        if(GLIDE_VER EQUAL 2)
            target_link_libraries(${TARGET_NAME} PRIVATE glide2x ${CMAKE_DL_LIBS})
        else()
            target_link_libraries(${TARGET_NAME} PRIVATE glide3x ${CMAKE_DL_LIBS})
        endif()
        if(NEED_MATH)
            target_link_libraries(${TARGET_NAME} PRIVATE m)
        endif()
    endif()
endfunction()

# --- Glide 2.x System Info Tool (Unified Pure C99) ---
add_executable(glide2_info glide_info/glide_info.c)
configure_glide_c99_tool(glide2_info 2 FALSE)
target_include_directories(glide2_info PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)

# --- Glide 3.x System Info Tool (Unified Pure C99) ---
add_executable(glide3_info glide_info/glide_info.c)
configure_glide_c99_tool(glide3_info 3 FALSE)
target_include_directories(glide3_info PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)

# --- Optional DOS/32 CauseWay glide_info.exe (Open Watcom v2) ---
if(NOT WIN32)
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
endif()

# --- Glide 2.x Benchmark Suite (Unified Pure C99) ---
set(GLIDE_BENCH_SOURCES
    glide_bench/glide_bench.c
    glide_bench/bench_hud.c
    glide_bench/bench_scenes.c
)

add_executable(glide2_bench ${GLIDE_BENCH_SOURCES})
configure_glide_c99_tool(glide2_bench 2 TRUE)
target_include_directories(glide2_bench PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench"
)

# --- Glide 3.x Benchmark Suite (Unified Pure C99) ---
add_executable(glide3_bench ${GLIDE_BENCH_SOURCES})
configure_glide_c99_tool(glide3_bench 3 TRUE)
target_include_directories(glide3_bench PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/glide_bench"
)

# --- Optional DOS/32 CauseWay glide_bench.exe (Open Watcom v2) ---
if(NOT WIN32)
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
endif()

# --- Glide 2.x Environment Variable Verifier (Unified Pure C99) ---
add_executable(glide2_envtest glide_envtest/glide_envtest.c)
configure_glide_c99_tool(glide2_envtest 2 FALSE)
target_include_directories(glide2_envtest PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide2"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)

# --- Glide 3.x Environment Variable Verifier (Unified Pure C99) ---
add_executable(glide3_envtest glide_envtest/glide_envtest.c)
configure_glide_c99_tool(glide3_envtest 3 FALSE)
target_include_directories(glide3_envtest PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/glide3"
    "${CMAKE_SOURCE_DIR}/include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)

# --- Optional DOS/32 CauseWay glide_envtest.exe (Open Watcom v2) ---
if(NOT WIN32)
add_dos_glide_executable(glide_envtest
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/glide_envtest/glide_envtest.c"
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
endif()



# add_subdirectory(snake)
