cmake_minimum_required(VERSION 3.20)

# Single source of truth for glide-ng SemVer version (reconfigures automatically on edit)
set(_GLIDE_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_GLIDE_VERSION_FILE}")
file(READ "${_GLIDE_VERSION_FILE}" _GLIDE_RAW_VERSION)
string(STRIP "${_GLIDE_RAW_VERSION}" GLIDE_VERSION_BASE)
if(GLIDE_VERSION_BASE MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9A-Za-z.-]+))?$")
    set(GLIDE_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(GLIDE_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(GLIDE_VERSION_PATCH "${CMAKE_MATCH_3}")
    set(GLIDE_VERSION_PRERELEASE "${CMAKE_MATCH_5}")
    set(GLIDE_VERSION_NUMERIC "${GLIDE_VERSION_MAJOR}.${GLIDE_VERSION_MINOR}.${GLIDE_VERSION_PATCH}")
else()
    message(FATAL_ERROR "Invalid SemVer in ${_GLIDE_VERSION_FILE}: '${GLIDE_VERSION_BASE}'")
endif()

project(glide-ng VERSION ${GLIDE_VERSION_NUMERIC} LANGUAGES CXX C)
if(WIN32 AND CMAKE_RC_COMPILER)
    enable_language(RC)
endif()
message(STATUS "Configuring glide-ng v${GLIDE_VERSION_BASE}")

# Enforce Modern C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Telemetry Configuration Option
option(ENABLE_TELEMETRY "Enable high-resolution profiling telemetry" OFF)
if(ENABLE_TELEMETRY)
    add_compile_definitions(ENABLE_TELEMETRY)
endif()

# Expose strict, clean compile warnings
option(GLIDE_STRICT_WARNINGS "Treat compiler warnings as errors on project targets" ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Discover System Graphics & Testing Dependencies
if(WIN32)
    # Zero-external-runtime-DLL Win32 build:
    # - Vulkan loaded dynamically at runtime via vulkan-1.dll (headers only)
    # - OpenGL 3.3 Core via Win32 WGL (opengl32.dll)
    # - Software via Win32 GDI StretchDIBits (gdi32.dll)
    # - Native Win32 HWND windowing (user32.dll, zero SDL2 dependency)
else()
    find_package(Vulkan REQUIRED)
    find_package(Catch2 QUIET)
    find_package(OpenMP REQUIRED)
    find_package(SDL2 REQUIRED)

    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
    endif()
endif()

# Setup Global Output Directories for compiled Binaries and Libraries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")

# --- Shader Compilation Infrastructure (Phase 2) ---
find_program(GLSLANG_VALIDATOR glslangValidator)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator compiler not found! Please install glslang-tools.")
endif()

set(GENERATED_SHADERS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders")
file(MAKE_DIRECTORY "${GENERATED_SHADERS_DIR}")
set(GENERATED_SHADER_HEADERS "")

function(compile_shader SHADER_PATH VAR_NAME HEADER_OUT)
    get_filename_component(SHADER_NAME ${SHADER_PATH} NAME)
    set(SPV_OUT "${CMAKE_CURRENT_BINARY_DIR}/${VAR_NAME}.spv")
    set(HEADER_FILE "${GENERATED_SHADERS_DIR}/${HEADER_OUT}")
    set(EXTRA_GLSL_FLAGS ${ARGN})
    
    # 1. Compile GLSL to SPIR-V
    add_custom_command(
        OUTPUT ${SPV_OUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${EXTRA_GLSL_FLAGS} ${SHADER_PATH} -o ${SPV_OUT}
        DEPENDS ${SHADER_PATH}
        COMMENT "Compiling GLSL shader ${SHADER_NAME} (${VAR_NAME}) to SPIR-V"
    )
    
    # 2. Convert SPIR-V to C++ Header
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_hex.py" ${SPV_OUT} ${HEADER_FILE} ${VAR_NAME}
        DEPENDS ${SPV_OUT} "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_hex.py"
        COMMENT "Converting SPIR-V binary ${VAR_NAME}.spv to C++ inline header ${HEADER_OUT}"
    )
    
    # Append to global list in parent scope
    set(GENERATED_SHADER_HEADERS ${GENERATED_SHADER_HEADERS} ${HEADER_FILE} PARENT_SCOPE)
endfunction()

# Register our shaders (single-source color.frag compiled for standard, dual-source blend,
# and W-buffer depth variants so Z-buffer/depth-off pipelines preserve hardware Early-Z)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.vert" "color_vert_spv" "color_vert.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.frag" "color_frag_spv" "color_frag.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.frag" "color_dualsrc_frag_spv" "color_dualsrc_frag.spv.h" -DUSE_DUAL_SRC_BLEND)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.frag" "color_wbuf_frag_spv" "color_wbuf_frag.spv.h" -DUSE_WBUFFER_DEPTH)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.frag" "color_dualsrc_wbuf_frag_spv" "color_dualsrc_wbuf_frag.spv.h" -DUSE_DUAL_SRC_BLEND -DUSE_WBUFFER_DEPTH)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/hud.vert" "hud_vert_spv" "hud_vert.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/hud.frag" "hud_frag_spv" "hud_frag.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/lfb_depth.frag" "lfb_depth_frag_spv" "lfb_depth_frag.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/msaa_depth_resolve.frag" "msaa_depth_resolve_frag_spv" "msaa_depth_resolve_frag.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/present_gamma.frag" "present_gamma_frag_spv" "present_gamma_frag.spv.h")

# Create a custom target for all shaders
add_custom_target(CompileShaders DEPENDS ${GENERATED_SHADER_HEADERS})

# --- Build Version & Git Hash Generation ---
set(GENERATED_VERSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_version")
set(GENERATED_VERSION_HEADER "${GENERATED_VERSION_DIR}/build_version.h")
# Generate once at configure time so IDEs see the header immediately,
# and refresh on every build (only rewrites on change to avoid rebuilds).
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${GENERATED_VERSION_HEADER}"
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_version_header.cmake"
)
add_custom_target(GenerateBuildVersion ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${GENERATED_VERSION_HEADER}"
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_version_header.cmake"
    COMMENT "Checking git revision for build_version.h"
)

# Optional Open Watcom v2 DOS/32 CauseWay & MinGW-w64 Win32 cross-build integration (Linux host)
if(NOT WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenWatcomDos.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MinGwWin32.cmake")
endif()

# Add Primary Source Directories
enable_testing()
add_subdirectory(src)
if(NOT WIN32)
    add_subdirectory(tests)
    add_subdirectory(tools)
    add_subdirectory(demos)
else()
    add_subdirectory(tools)
endif()
