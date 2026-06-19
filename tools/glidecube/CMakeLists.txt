# 1. Compile Glide 2 cube executable
add_executable(glide2_cube glidecube.cpp ${CMAKE_SOURCE_DIR}/src/core/EmbeddedAssets.cpp)
target_compile_definitions(glide2_cube PRIVATE GLIDE_VERSION=2)
target_link_libraries(glide2_cube PRIVATE tlib2 tool_info2 glide2x)
target_include_directories(glide2_cube PRIVATE 
    ${CMAKE_SOURCE_DIR}/include/glide2
    ${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/glide2x/cvg/glide/tests
    ${CMAKE_SOURCE_DIR}/src
)

# 2. Compile Glide 3 cube executable
add_executable(glide3_cube glidecube.cpp ${CMAKE_SOURCE_DIR}/src/core/EmbeddedAssets.cpp)
target_compile_definitions(glide3_cube PRIVATE GLIDE_VERSION=3)
target_link_libraries(glide3_cube PRIVATE tlib3 tool_info3 glide3x)
target_include_directories(glide3_cube PRIVATE 
    ${CMAKE_SOURCE_DIR}/include/glide3
    ${CMAKE_SOURCE_DIR}/refs/3dfx-Glide-API/glide3x/cvg/glide3/tests
    ${CMAKE_SOURCE_DIR}/src
)
