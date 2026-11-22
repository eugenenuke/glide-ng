if(GLIDE_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror)
endif()

# Keep default source builds dynamically linked against system runtimes.
# Packaging scripts/CI may enable this option on Linux to avoid hard-pinning
# libstdc++/libgcc versions on older host distributions.
option(GLIDE_STATIC_CPP_RUNTIME
    "Statically link libstdc++ and libgcc into Linux libglide*.so release libraries"
    OFF)

# 1. Define Core Sources
set(CORE_SOURCES
    core/WrapperConfig.cpp
    core/SimdDispatch.cpp
    core/SimdKernels.cpp
    core/SimdKernelsAvx2.cpp
    core/SimdKernelsAvx512.cpp
    core/Logger.cpp
    core/IniConfigLoader.cpp
    core/VertexLayout.cpp
    core/BackendManager.cpp
    core/TextureManager.cpp
    core/Telemetry.cpp
    core/HudOverlay.cpp
    core/GlideSplashAnimator.cpp
    core/3dfParser.cpp
    core/MathUtils.cpp
    core/LfbManager.cpp
    core/NativeSurfaceHost.cpp
    core/GlideEntryCommon.cpp
    backends/GlideStateTracker.cpp
    backends/CpuLfbStaging.cpp
    backends/SoftwareBackendBase.cpp
    backends/vulkan/VulkanBackend.cpp
    backends/vulkan/VulkanPipeline.cpp
    backends/vulkan/VulkanTextures.cpp
    backends/vulkan/VulkanLfb.cpp
    backends/vulkan/VulkanDraw.cpp
    backends/vulkan/VulkanHud.cpp
    backends/opengl_es/OpenGLESBackend.cpp
    backends/opengl_es/OpenGLESShaders.cpp
    backends/opengl_es/OpenGLESTextures.cpp
    backends/opengl_es/OpenGLESDraw.cpp
    backends/software/SoftwareBackend.cpp
    backends/software/SoftwareCombiner.cpp
    backends/software/SoftwareRasterizer.cpp
    backends/software/SoftwareRasterizerSimd.cpp
    backends/software/SoftwareRasterizerAvx512.cpp
)

# --- TARGET 1: Universal GlideWrapperCore ---
add_library(GlideWrapperCore STATIC ${CORE_SOURCES})
# Strict per-TU ISA isolation: baseline core/scalar TUs never emit AVX2/AVX-512
# instructions, keeping runtime CPUID dispatch 100% safe across all x86 CPUs.
set_source_files_properties(
    core/SimdKernelsAvx2.cpp
    backends/software/SoftwareRasterizerSimd.cpp
    PROPERTIES COMPILE_FLAGS "-mavx2 -mfma"
)
set_source_files_properties(
    core/SimdKernelsAvx512.cpp
    backends/software/SoftwareRasterizerAvx512.cpp
    PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512dq -mavx512bw -mavx512vl"
)
set_target_properties(GlideWrapperCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(GlideWrapperCore PRIVATE
        -ffunction-sections
        -fdata-sections
    )
    if(NOT WIN32)
        target_compile_options(GlideWrapperCore PRIVATE
            -fvisibility=hidden
            -fvisibility-inlines-hidden
        )
    endif()
endif()
target_include_directories(GlideWrapperCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_BINARY_DIR}/generated_shaders"
    "${CMAKE_BINARY_DIR}/generated_version"
)
add_dependencies(GlideWrapperCore CompileShaders GenerateBuildVersion)
if(WIN32)
    # Stage isolated Vulkan headers so cross-compiling never puts Linux /usr/include on search path
    set(_WIN32_VK_STAGE "${CMAKE_BINARY_DIR}/win32_vk_inc")
    file(MAKE_DIRECTORY "${_WIN32_VK_STAGE}")
    set(_VK_HEADER_ROOT "")
    if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include/vulkan/vulkan.hpp")
        set(_VK_HEADER_ROOT "$ENV{VULKAN_SDK}/Include")
    elseif(EXISTS "/usr/include/vulkan/vulkan.hpp")
        set(_VK_HEADER_ROOT "/usr/include")
    endif()
    if(NOT _VK_HEADER_ROOT)
        message(FATAL_ERROR "Vulkan headers (vulkan/vulkan.hpp + vulkan/vulkan_win32.h) not found! Install libvulkan-dev or set VULKAN_SDK.")
    endif()
    if(NOT EXISTS "${_WIN32_VK_STAGE}/vulkan")
        file(CREATE_LINK "${_VK_HEADER_ROOT}/vulkan" "${_WIN32_VK_STAGE}/vulkan" SYMBOLIC)
    endif()
    if(EXISTS "${_VK_HEADER_ROOT}/vk_video" AND NOT EXISTS "${_WIN32_VK_STAGE}/vk_video")
        file(CREATE_LINK "${_VK_HEADER_ROOT}/vk_video" "${_WIN32_VK_STAGE}/vk_video" SYMBOLIC)
    endif()
    target_include_directories(GlideWrapperCore SYSTEM PUBLIC "${_WIN32_VK_STAGE}")
    target_compile_definitions(GlideWrapperCore PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_SPACESHIP_OPERATOR
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MINGW)
        # Protect 32-bit AVX2/SSE code from 4-byte-aligned 1990s Win32 caller stacks
        target_compile_options(GlideWrapperCore PUBLIC -mstackrealign)
    endif()
    target_link_libraries(GlideWrapperCore PUBLIC user32 gdi32)
else()
    target_link_libraries(GlideWrapperCore PUBLIC Vulkan::Vulkan SDL2::SDL2 GLESv2 OpenMP::OpenMP_CXX ${CMAKE_DL_LIBS})
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(GlideWrapperCore PUBLIC X11::X11 X11::Xext)
    target_compile_definitions(GlideWrapperCore PUBLIC HAVE_X11=1)

    set(_GLIDE_WSI_X11 "ON")
    set(_GLIDE_WSI_WAYLAND "OFF")
    set(_GLIDE_WSI_WAYLAND_EGL "OFF")
    set(_GLIDE_WSI_EGL "OFF")

    # Discover Wayland Client, Wayland-EGL, and EGL via PkgConfig
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(WAYLAND_CLIENT wayland-client)
        if(WAYLAND_CLIENT_FOUND)
            target_include_directories(GlideWrapperCore PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
            target_link_libraries(GlideWrapperCore PUBLIC ${WAYLAND_CLIENT_LIBRARIES})
            target_compile_definitions(GlideWrapperCore PUBLIC HAVE_WAYLAND=1)
            set(_GLIDE_WSI_WAYLAND "ON (${WAYLAND_CLIENT_VERSION})")
        endif()

        pkg_check_modules(WAYLAND_EGL wayland-egl)
        if(WAYLAND_EGL_FOUND)
            target_include_directories(GlideWrapperCore PRIVATE ${WAYLAND_EGL_INCLUDE_DIRS})
            target_link_libraries(GlideWrapperCore PUBLIC ${WAYLAND_EGL_LIBRARIES})
            target_compile_definitions(GlideWrapperCore PUBLIC HAVE_WAYLAND_EGL=1)
            set(_GLIDE_WSI_WAYLAND_EGL "ON (${WAYLAND_EGL_VERSION})")
        endif()

        pkg_check_modules(EGL egl)
        if(EGL_FOUND)
            target_include_directories(GlideWrapperCore PRIVATE ${EGL_INCLUDE_DIRS})
            target_link_libraries(GlideWrapperCore PUBLIC ${EGL_LIBRARIES})
            target_compile_definitions(GlideWrapperCore PUBLIC HAVE_EGL=1)
            set(_GLIDE_WSI_EGL "ON (${EGL_VERSION})")
        endif()
    endif()

    message(STATUS "WSI Backends: HAVE_X11=${_GLIDE_WSI_X11}, HAVE_WAYLAND=${_GLIDE_WSI_WAYLAND}, HAVE_WAYLAND_EGL=${_GLIDE_WSI_WAYLAND_EGL}, HAVE_EGL=${_GLIDE_WSI_EGL}")
endif()


# Apply shared linker export/dead-code stripping options to frontend DSOs/DLLs.
function(configure_glide_shared_frontend TARGET_NAME VERSION_SCRIPT)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET_NAME} PRIVATE
            -ffunction-sections
            -fdata-sections
        )
    endif()
    if(WIN32)
        if(MINGW)
            target_link_options(${TARGET_NAME} PRIVATE
                -static
                -mstackrealign
                -Wl,--gc-sections
            )
        endif()
    elseif(UNIX AND NOT APPLE)
        target_link_options(${TARGET_NAME} PRIVATE
            "-Wl,--version-script=${VERSION_SCRIPT}"
            -Wl,-Bsymbolic-functions
            -Wl,--gc-sections
            -Wl,--exclude-libs,ALL
        )
        set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS "${VERSION_SCRIPT}")
        if(GLIDE_STATIC_CPP_RUNTIME AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(${TARGET_NAME} PRIVATE
                -static-libstdc++
                -static-libgcc
            )
        endif()
    endif()
endfunction()

# 2. Glide 2.11 Frontend Export Library (libglide.so / glide.dll)
set(GLIDE211_SOURCES glide211/glide211_entry.cpp)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glide211/glide.def")
    list(APPEND GLIDE211_SOURCES glide211/glide.def)
endif()
if(WIN32 AND CMAKE_RC_COMPILER)
    configure_file(glide211/glide.rc.in "${CMAKE_CURRENT_BINARY_DIR}/glide.rc" @ONLY)
    list(APPEND GLIDE211_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/glide.rc")
endif()
add_library(glide SHARED ${GLIDE211_SOURCES})
if(WIN32)
    set_target_properties(glide PROPERTIES PREFIX "" OUTPUT_NAME "glide")
else()
    set_target_properties(glide PROPERTIES VERSION 2.11.0 SOVERSION 0)
endif()
configure_glide_shared_frontend(glide "${CMAKE_CURRENT_SOURCE_DIR}/glide211/libglide.ver")
target_include_directories(glide PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide211"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
)
target_link_libraries(glide PRIVATE GlideWrapperCore)


# 3. Universal Glide 2 Frontend Export Library (libglide2x.so / glide2x.dll)
set(GLIDE2_SOURCES glide2/glide2_entry.cpp)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glide2/glide2x.def")
    list(APPEND GLIDE2_SOURCES glide2/glide2x.def)
endif()
if(WIN32 AND CMAKE_RC_COMPILER)
    configure_file(glide2/glide2x.rc.in "${CMAKE_CURRENT_BINARY_DIR}/glide2x.rc" @ONLY)
    list(APPEND GLIDE2_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/glide2x.rc")
endif()
add_library(glide2x SHARED ${GLIDE2_SOURCES})
if(WIN32)
    set_target_properties(glide2x PROPERTIES PREFIX "" OUTPUT_NAME "glide2x")
else()
    set_target_properties(glide2x PROPERTIES VERSION 2.0.0 SOVERSION 0)
endif()
configure_glide_shared_frontend(glide2x "${CMAKE_CURRENT_SOURCE_DIR}/glide2/libglide2x.ver")
target_include_directories(glide2x PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
)
target_link_libraries(glide2x PRIVATE GlideWrapperCore)


# 4. Glide 3 Frontend Export Library (libglide3x.so / glide3x.dll)
set(GLIDE3_SOURCES glide3/glide3_entry.cpp)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glide3/glide3x.def")
    list(APPEND GLIDE3_SOURCES glide3/glide3x.def)
endif()
if(WIN32 AND CMAKE_RC_COMPILER)
    configure_file(glide3/glide3x.rc.in "${CMAKE_CURRENT_BINARY_DIR}/glide3x.rc" @ONLY)
    list(APPEND GLIDE3_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/glide3x.rc")
endif()
add_library(glide3x SHARED ${GLIDE3_SOURCES})
if(WIN32)
    set_target_properties(glide3x PROPERTIES PREFIX "" OUTPUT_NAME "glide3x")
else()
    set_target_properties(glide3x PROPERTIES VERSION 3.0.0 SOVERSION 0)
endif()
configure_glide_shared_frontend(glide3x "${CMAKE_CURRENT_SOURCE_DIR}/glide3/libglide3x.ver")
target_include_directories(glide3x PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
)
target_link_libraries(glide3x PRIVATE GlideWrapperCore)

# Generate canonical 3dfx Win32 import libraries (mapping MinGW __stdcall
# symbols Func@N -> DLL export _Func@N) so Win32 tools compiled with MinGW
# import the exact same _Func@N names as authentic 1990s MSVC/Watcom games
# (drop-in compatible with glide-ng, real 3dfx drivers, nGlide, and dgVoodoo2).
if(WIN32 AND MINGW AND CMAKE_DLLTOOL)
    function(add_win32_3dfx_implib DLL_TARGET DEF_FILE)
        set(_imp_def "${CMAKE_CURRENT_BINARY_DIR}/${DLL_TARGET}_3dfx_imp.def")
        set(_imp_lib "${CMAKE_BINARY_DIR}/lib/lib${DLL_TARGET}_3dfx.dll.a")
        file(STRINGS "${DEF_FILE}" _def_lines REGEX "^[ \t]*_[^ \t]+@[0-9]+[ \t]*=")
        set(_imp_content "LIBRARY ${DLL_TARGET}.dll\nEXPORTS\n")
        foreach(_line IN LISTS _def_lines)
            if(_line MATCHES "^[ \t]*(_([^ \t]+@[0-9]+))[ \t]*=")
                string(APPEND _imp_content "  ${CMAKE_MATCH_2} == ${CMAKE_MATCH_1}\n")
            endif()
        endforeach()
        file(WRITE "${_imp_def}" "${_imp_content}")
        add_custom_command(
            OUTPUT "${_imp_lib}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/lib"
            COMMAND "${CMAKE_DLLTOOL}" -d "${_imp_def}" -l "${_imp_lib}" -D "${DLL_TARGET}.dll"
            DEPENDS "${DEF_FILE}" "${_imp_def}"
            COMMENT "Generating canonical 3dfx Win32 import library for ${DLL_TARGET}.dll"
            VERBATIM
        )
        add_custom_target(${DLL_TARGET}_3dfx_implib_gen DEPENDS "${_imp_lib}" ${DLL_TARGET})
        add_library(${DLL_TARGET}_3dfx_implib INTERFACE)
        add_dependencies(${DLL_TARGET}_3dfx_implib ${DLL_TARGET}_3dfx_implib_gen)
        target_link_libraries(${DLL_TARGET}_3dfx_implib INTERFACE "${_imp_lib}")
    endfunction()

    add_win32_3dfx_implib(glide "${CMAKE_CURRENT_SOURCE_DIR}/glide211/glide.def")
    add_win32_3dfx_implib(glide2x "${CMAKE_CURRENT_SOURCE_DIR}/glide2/glide2x.def")
    add_win32_3dfx_implib(glide3x "${CMAKE_CURRENT_SOURCE_DIR}/glide3/glide3x.def")
endif()

# Optional clean-room DOS/32 Glide 2.x overlay (GLIDE2X.OVL) for DOSBox-SVN / DOSBox-X
if(COMMAND add_dos_glide_ovl)
    add_dos_glide_ovl(glide2x
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/dos_ovl/glide2x_ovl.c"
        LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/dos_ovl/glide2x_ovl.lnk"
    )
endif()
