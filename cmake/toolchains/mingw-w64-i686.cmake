# CMake Toolchain File: 32-bit Windows (i686) via MinGW-w64 (POSIX threads)
#
# Usage:
#   cmake -B build_win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build_win32 -j$(nproc)
#
# Produces zero-external-runtime-DLL 32-bit PE binaries under build_win32/bin/:
#   glide2x.dll, glide3x.dll, glide2_info.exe, glide3_info.exe,
#   glide2_bench.exe, glide3_bench.exe, glide2_envtest.exe, glide3_envtest.exe

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

# Prefer Debian/Ubuntu explicit -posix compilers (required for std::thread/std::mutex),
# falling back cleanly to standard i686-w64-mingw32-gcc/g++ on Arch/Fedora.
find_program(MINGW32_C_COMPILER
    NAMES i686-w64-mingw32-gcc-posix i686-w64-mingw32-gcc)
find_program(MINGW32_CXX_COMPILER
    NAMES i686-w64-mingw32-g++-posix i686-w64-mingw32-g++)
find_program(MINGW32_RC_COMPILER
    NAMES i686-w64-mingw32-windres)

if(NOT MINGW32_C_COMPILER OR NOT MINGW32_CXX_COMPILER)
    message(FATAL_ERROR
        "MinGW-w64 32-bit (i686) POSIX compiler not found. "
        "On Debian/Ubuntu install: sudo apt-get install g++-mingw-w64-i686-posix gcc-mingw-w64-i686-posix mingw-w64-tools")
endif()

set(CMAKE_C_COMPILER "${MINGW32_C_COMPILER}")
set(CMAKE_CXX_COMPILER "${MINGW32_CXX_COMPILER}")
if(MINGW32_RC_COMPILER)
    set(CMAKE_RC_COMPILER "${MINGW32_RC_COMPILER}")
endif()

set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)

# Run host build tools (glslangValidator, python3) natively on the Linux host;
# resolve target libraries and headers strictly inside the MinGW sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
