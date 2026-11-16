cmake_minimum_required(VERSION 3.20)
project(3dfxGlideWrapper CXX C)

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
find_package(Vulkan REQUIRED)
find_package(Catch2 QUIET)
find_package(OpenMP REQUIRED)
find_package(SDL2 REQUIRED)

# Optional external 3dfx reference SDK (untracked local directory used only by legacy SDK tests/demos)
set(GLIDE_REF_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/refs/3dfx-Glide-API")
if(EXISTS "${GLIDE_REF_SDK_DIR}/swlibs/fxmisc/linutil.c")
    set(GLIDE_HAS_REF_SDK TRUE)
else()
    set(GLIDE_HAS_REF_SDK FALSE)
    message(STATUS "Optional 3dfx reference SDK not found at ${GLIDE_REF_SDK_DIR}; skipping legacy SDK-dependent tests and demos")
endif()

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
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

# Register our shaders (single-source color.frag compiled for standard & dual-source blend)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.vert" "color_vert_spv" "color_vert.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.frag" "color_frag_spv" "color_frag.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/color.frag" "color_dualsrc_frag_spv" "color_dualsrc_frag.spv.h" -DUSE_DUAL_SRC_BLEND)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/hud.vert" "hud_vert_spv" "hud_vert.spv.h")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/hud.frag" "hud_frag_spv" "hud_frag.spv.h")

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

# Optional Open Watcom v2 DOS/32 CauseWay cross-build integration
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenWatcomDos.cmake")

# Add Primary Source Directories
enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(tools)
add_subdirectory(demos)
