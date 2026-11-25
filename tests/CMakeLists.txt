if(GLIDE_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror)
endif()

if(Catch2_FOUND)
    # NOTE: Each Catch2 test binary links GlideWrapperCore statically together
    # with its own frontend entry translation unit and exports symbols via
    # -rdynamic. This guarantees one coherent in-process singleton set for
    # white-box tests while production libglide*.so libraries hide all
    # internal C++ symbols behind their ELF version scripts. Black-box ABI /
    # export tests still dlopen the real built shared libraries under lib/.

    # --- Glide 2.11 (libglide.so / glide.dll) Catch2 Test Suite ---
    add_executable(Glide211WrapperTests
        glide211/test_glide211_abi.cpp
        "${CMAKE_SOURCE_DIR}/src/glide211/glide211_entry.cpp"
    )
    target_include_directories(Glide211WrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/include/glide211"
        "${CMAKE_SOURCE_DIR}/include/glide2"
        "${CMAKE_SOURCE_DIR}/include/3dfx"
    )
    target_compile_definitions(Glide211WrapperTests PRIVATE
        -D__linux__
        -DGLIDE_VERSION=2
    )
    target_link_options(Glide211WrapperTests PRIVATE -rdynamic)
    target_link_libraries(Glide211WrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        ${CMAKE_DL_LIBS}
        -lm
    )
    add_dependencies(Glide211WrapperTests glide)

    # --- Glide 2.x (libglide2x.so / glide2x.dll) Catch2 Test Suite ---
    add_executable(Glide2xWrapperTests
        glide2/test_glide2_abi.cpp
        glide2/test_glide2_core_units.cpp
        glide2/test_glide2_geometry.cpp
        glide2/test_glide2_pipeline.cpp
        glide2/test_glide2_lfb.cpp
        glide2/test_glide2_textures.cpp
        glide2/test_glide2_config_simd.cpp
        glide2/test_glide2_regressions.cpp
        "${CMAKE_SOURCE_DIR}/src/glide2/glide2_entry.cpp"
    )
    target_include_directories(Glide2xWrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/include/glide2"
        "${CMAKE_SOURCE_DIR}/include/3dfx"
    )
    target_compile_definitions(Glide2xWrapperTests PRIVATE
        -D__linux__
        -DGLIDE_VERSION=2
    )
    target_link_options(Glide2xWrapperTests PRIVATE -rdynamic)
    target_link_libraries(Glide2xWrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        ${CMAKE_DL_LIBS}
        -lm
    )
    add_dependencies(Glide2xWrapperTests glide2x glide3x)
    if(GLIDE_USE_PCH)
        target_precompile_headers(Glide2xWrapperTests PRIVATE
            <catch2/catch_test_macros.hpp>
            <algorithm>
            <cmath>
            <cstdint>
            <cstring>
            <string>
            <vector>
        )
        set_source_files_properties(
            "${CMAKE_SOURCE_DIR}/src/glide2/glide2_entry.cpp"
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON
        )
    endif()

    # --- Glide 3.x (libglide3x.so / glide3x.dll) Catch2 Test Suite ---
    add_executable(Glide3xWrapperTests
        glide3/test_glide3_state.cpp
        glide3/test_glide3_pipeline.cpp
        glide3/test_glide3_textures.cpp
        glide3/test_glide3_vsa100.cpp
        glide3/test_glide3_lfb_depth.cpp
        glide3/test_glide3_infra.cpp
        glide3/test_glide3_simd.cpp
        glide3/test_glide3_simd_vec.cpp
        glide3/test_glide3_catalog.cpp
        "${CMAKE_SOURCE_DIR}/src/glide3/glide3_entry.cpp"
    )
    set_source_files_properties(glide3/test_glide3_simd_vec.cpp
        PROPERTIES COMPILE_FLAGS "-mavx2")
    target_include_directories(Glide3xWrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/include/glide3"
        "${CMAKE_SOURCE_DIR}/include/3dfx"
    )
    target_compile_definitions(Glide3xWrapperTests PRIVATE
        -D__linux__
        -DGLIDE_VERSION=3
        "-DGLIDE_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
    )
    target_link_options(Glide3xWrapperTests PRIVATE -rdynamic)
    target_link_libraries(Glide3xWrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        ${CMAKE_DL_LIBS}
        -lm
    )
    add_dependencies(Glide3xWrapperTests glide2x glide3x)
    if(GLIDE_USE_PCH)
        target_precompile_headers(Glide3xWrapperTests PRIVATE
            <catch2/catch_test_macros.hpp>
            <algorithm>
            <cmath>
            <cstdint>
            <cstring>
            <string>
            <vector>
        )
        set_source_files_properties(
            glide3/test_glide3_simd_vec.cpp
            "${CMAKE_SOURCE_DIR}/src/glide3/glide3_entry.cpp"
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON
        )
    endif()

    include(CTest)
    include(Catch)
    catch_discover_tests(Glide211WrapperTests)
    catch_discover_tests(Glide2xWrapperTests)
    catch_discover_tests(Glide3xWrapperTests)

    find_program(GLIDE_BASH_EXECUTABLE bash)
    if(GLIDE_BASH_EXECUTABLE)
        add_test(
            NAME GlideDosboxLauncherSmoke
            COMMAND "${GLIDE_BASH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/scripts/test_glide_dosbox.sh"
        )
    endif()
else()
    message(STATUS "Catch2 not found; skipping automated Catch2 test suites")
endif()
