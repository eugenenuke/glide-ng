if(Catch2_FOUND)
    # --- Glide 2.11 (glide.dll / libglide.so) Catch2 Test Suite ---
    add_executable(Glide211WrapperTests
        test_glide211.cpp
    )

    target_include_directories(Glide211WrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide211"
    )

    target_compile_definitions(Glide211WrapperTests PRIVATE
        -D__linux__
        -DGLIDE_VERSION=2
    )

    target_link_libraries(Glide211WrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        glide
        ${CMAKE_DL_LIBS}
        -lm
    )

    # --- Glide 2.x Catch2 Test Suite ---
    add_executable(Glide2xWrapperTests
        test_architecture.cpp
    )

    target_include_directories(Glide2xWrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide2"
    )

    target_compile_definitions(Glide2xWrapperTests PRIVATE
        -D__linux__
        -DGLIDE_VERSION=2
    )

    target_link_libraries(Glide2xWrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        glide2x
        ${CMAKE_DL_LIBS}
        -lm
    )

    # --- Glide 3.x Catch2 Test Suite ---
    add_executable(Glide3xWrapperTests
        test_glide3.cpp
        test_simd_vec.cpp
    )

    set_source_files_properties(test_simd_vec.cpp PROPERTIES COMPILE_FLAGS "-mavx2")

    target_include_directories(Glide3xWrapperTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
    )

    target_compile_definitions(Glide3xWrapperTests PRIVATE
        -D__linux__
        -DGLIDE_VERSION=3
        "-DGLIDE_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
    )

    target_link_libraries(Glide3xWrapperTests PRIVATE
        GlideWrapperCore
        Catch2::Catch2WithMain
        glide3x
        ${CMAKE_DL_LIBS}
        -lm
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(Glide211WrapperTests)
    catch_discover_tests(Glide2xWrapperTests)
    catch_discover_tests(Glide3xWrapperTests)
else()
    message(STATUS "Catch2 not found; skipping Glide2xWrapperTests and Glide3xWrapperTests")
endif()

# --- Headless Software Rasterizer Visual Validation Tool ---
add_executable(sw_validate sw_validate.cpp)
target_include_directories(sw_validate PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/3dfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/glide3"
)
target_compile_definitions(sw_validate PRIVATE -D__linux__ -DGLIDE_VERSION=3)
target_link_libraries(sw_validate PRIVATE
    GlideWrapperCore
    glide3x
    ${CMAKE_DL_LIBS}
    -lm
)
