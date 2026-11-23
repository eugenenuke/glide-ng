# Optional MinGW-w64 32-bit (i686) Win32 cross-build integration.
# Automatically detects i686-w64-mingw32-gcc/g++ (POSIX threads) on Linux hosts
# and builds glide.dll, glide2x.dll, glide3x.dll, and Win32 tools under ${CMAKE_BINARY_DIR}/win32/bin
# alongside Linux and DOS/32 outputs; never fails Linux builds when MinGW is absent.

option(GLIDE_BUILD_WIN32
    "Automatically cross-build 32-bit Win32 DLLs and tools when MinGW-w64 i686 is installed"
    ON)

set(GLIDE_HAS_MINGW_WIN32 FALSE)

if(NOT GLIDE_BUILD_WIN32)
    message(STATUS "Win32 MinGW-w64 cross-build disabled (-DGLIDE_BUILD_WIN32=OFF)")
    return()
endif()

find_program(_GLIDE_MINGW32_CC
    NAMES i686-w64-mingw32-gcc-posix i686-w64-mingw32-gcc)
find_program(_GLIDE_MINGW32_CXX
    NAMES i686-w64-mingw32-g++-posix i686-w64-mingw32-g++)

set(_GLIDE_WIN32_HAS_VK_HEADERS FALSE)
if((DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include/vulkan/vulkan.hpp")
   OR EXISTS "/usr/include/vulkan/vulkan.hpp")
    set(_GLIDE_WIN32_HAS_VK_HEADERS TRUE)
endif()

if(_GLIDE_MINGW32_CC AND _GLIDE_MINGW32_CXX AND _GLIDE_WIN32_HAS_VK_HEADERS)
    set(GLIDE_WIN32_BUILD_DIR "${CMAKE_BINARY_DIR}/win32")
    set(_GLIDE_WIN32_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    if(NOT _GLIDE_WIN32_BUILD_TYPE)
        set(_GLIDE_WIN32_BUILD_TYPE "Release")
    endif()

    set(_GLIDE_WIN32_TOOLCHAIN "${CMAKE_SOURCE_DIR}/cmake/toolchains/mingw-w64-i686.cmake")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -S "${CMAKE_SOURCE_DIR}"
            -B "${GLIDE_WIN32_BUILD_DIR}"
            -G "${CMAKE_GENERATOR}"
            "-DCMAKE_TOOLCHAIN_FILE=${_GLIDE_WIN32_TOOLCHAIN}"
            "-DCMAKE_BUILD_TYPE=${_GLIDE_WIN32_BUILD_TYPE}"
            "-DENABLE_TELEMETRY=${ENABLE_TELEMETRY}"
            "-DGLIDE_STRICT_WARNINGS=${GLIDE_STRICT_WARNINGS}"
            "-DGLIDE_USE_PCH=${GLIDE_USE_PCH}"
        RESULT_VARIABLE _win32_cfg_res
        OUTPUT_VARIABLE _win32_cfg_out
        ERROR_VARIABLE _win32_cfg_err
    )

    if(_win32_cfg_res EQUAL 0)
        set(GLIDE_HAS_MINGW_WIN32 TRUE)
        message(STATUS "MinGW-w64 i686 detected (${_GLIDE_MINGW32_CXX}); enabling Win32 DLLs & tools (${_GLIDE_WIN32_BUILD_TYPE}) -> ${GLIDE_WIN32_BUILD_DIR}/bin")

        # Unset MAKEFLAGS/MFLAGS so nested parallel builds avoid GNU Make jobserver warnings.
        add_custom_target(win32_tools ALL
            COMMAND "${CMAKE_COMMAND}" -E env --unset=MAKEFLAGS --unset=MFLAGS
                "${CMAKE_COMMAND}" --build "${GLIDE_WIN32_BUILD_DIR}" --parallel
            COMMENT "Building 32-bit Win32 DLLs & tools -> ${GLIDE_WIN32_BUILD_DIR}/bin"
            USES_TERMINAL
        )
        add_custom_target(win32_dlls
            COMMAND "${CMAKE_COMMAND}" -E env --unset=MAKEFLAGS --unset=MFLAGS
                "${CMAKE_COMMAND}" --build "${GLIDE_WIN32_BUILD_DIR}" --parallel --target glide glide2x glide3x
            COMMENT "Building 32-bit Win32 glide.dll, glide2x.dll & glide3x.dll -> ${GLIDE_WIN32_BUILD_DIR}/bin"
            USES_TERMINAL
        )
    else()
        message(WARNING "MinGW-w64 i686 detected (${_GLIDE_MINGW32_CXX}), but Win32 sub-configure failed (skipping optional Win32 cross-build):\n${_win32_cfg_err}")
    endif()
elseif(_GLIDE_MINGW32_CC AND _GLIDE_MINGW32_CXX)
    message(STATUS "MinGW-w64 i686 detected, but Vulkan C++ headers (vulkan/vulkan.hpp) not found; skipping optional Win32 cross-build")
else()
    message(STATUS "MinGW-w64 i686 not found; skipping optional Win32 DLL/tool cross-builds")
endif()
