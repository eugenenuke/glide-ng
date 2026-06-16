cmake_minimum_required(VERSION 3.20)
project(3dfxGlideWrapper CXX C)

# Enforce Modern C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Expose strict, clean compile warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Discover System Graphics & Testing Dependencies
find_package(Vulkan REQUIRED)
find_package(Catch2 REQUIRED)

# Setup Global Output Directories for compiled Binaries and Libraries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")

# Add Primary Source Directories
add_subdirectory(src)
add_subdirectory(tests)
