if(GLIDE_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror)
endif()

# 1. Define Core Sources
set(CORE_SOURCES
    core/WrapperConfig.cpp
    core/Logger.cpp
    core/JsonConfigLoader.cpp
    core/VertexLayout.cpp
    core/BackendManager.cpp
    core/TextureManager.cpp
    core/Telemetry.cpp
    core/EmbeddedAssets.cpp
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
    backends/opengl_es/OpenGLESBackend.cpp
    backends/opengl_es/OpenGLESShaders.cpp
    backends/opengl_es/OpenGLESTextures.cpp
    backends/opengl_es/OpenGLESDraw.cpp
    backends/software/SoftwareBackend.cpp
    backends/software/SoftwareCombiner.cpp
    backends/software/SoftwareRasterizer.cpp
    backends/software/SoftwareRasterizerSimd.cpp
)

# --- TARGET 1: Universal GlideWrapperCore ---
add_library(GlideWrapperCore STATIC ${CORE_SOURCES})
set_source_files_properties(
    backends/SoftwareBackendBase.cpp
    backends/software/SoftwareBackend.cpp
    backends/software/SoftwareCombiner.cpp
    backends/software/SoftwareRasterizer.cpp
    backends/software/SoftwareRasterizerSimd.cpp
    PROPERTIES COMPILE_FLAGS "-mavx2"
)
set_target_properties(GlideWrapperCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
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
target_link_libraries(GlideWrapperCore PUBLIC Vulkan::Vulkan SDL2::SDL2 GLESv2 OpenMP::OpenMP_CXX ${CMAKE_DL_LIBS})
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


# 2. Universal Glide 2 Frontend Export Library (libglide2x.so)
add_library(glide2x SHARED glide2/glide2_entry.cpp)
set_target_properties(glide2x PROPERTIES VERSION 2.0.0 SOVERSION 0)
target_include_directories(glide2x PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/3dfx"
)
target_link_libraries(glide2x PRIVATE GlideWrapperCore)


# 5. Glide 3 Frontend Export Library (libglide3x.so)
add_library(glide3x SHARED glide3/glide3_entry.cpp)
set_target_properties(glide3x PROPERTIES VERSION 3.0.0 SOVERSION 0)
target_include_directories(glide3x PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
)
target_link_libraries(glide3x PRIVATE GlideWrapperCore)
