# 1. Unified Core Emulation Engine (Static Library)
add_library(GlideWrapperCore STATIC
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
    backends/SoftwareBackendBase.cpp
    backends/vulkan/VulkanBackend.cpp
    backends/opengl_es/OpenGLESBackend.cpp
    backends/software/SoftwareBackend.cpp
)

# Enable AVX2 instruction set specifically for the Software Reference Backend files
set_source_files_properties(
    backends/SoftwareBackendBase.cpp
    backends/software/SoftwareBackend.cpp
    PROPERTIES COMPILE_FLAGS "-mavx2"
)


# Mandatory when linking a Static library into a Shared library (.so / .dll)
set_target_properties(GlideWrapperCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(GlideWrapperCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_BINARY_DIR}/generated_shaders"
)
add_dependencies(GlideWrapperCore CompileShaders)
target_link_libraries(GlideWrapperCore PUBLIC Vulkan::Vulkan SDL2::SDL2 GLESv2 OpenMP::OpenMP_CXX)
if(UNIX AND NOT APPLE)
    target_link_libraries(GlideWrapperCore PUBLIC X11::X11)
endif()

# 2. Glide 2 Frontend Export Library (glide2x.dll / libglide2x.so)
add_library(glide2x SHARED 
    glide2/glide2_entry.cpp
)
set_target_properties(glide2x PROPERTIES
    VERSION 2.0.0
    SOVERSION 0
)
set_target_properties(glide2x PROPERTIES
    VERSION 2.0.0
    SOVERSION 0
)
target_include_directories(glide2x PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/3dfx"
)
target_link_libraries(glide2x PRIVATE GlideWrapperCore)

# 3. Glide 3 Frontend Export Library (glide3x.dll / libglide3x.so)
add_library(glide3x SHARED 
    glide3/glide3_entry.cpp
)
set_target_properties(glide3x PROPERTIES
    VERSION 3.0.0
    SOVERSION 0
)
set_target_properties(glide3x PROPERTIES
    VERSION 3.0.0
    SOVERSION 0
)
target_include_directories(glide3x PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
)
target_link_libraries(glide3x PRIVATE GlideWrapperCore)
