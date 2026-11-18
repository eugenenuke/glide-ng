# Generates build_version.h from the root VERSION file and git metadata.
# Rewrites OUTPUT_FILE only when content changes to avoid redundant rebuilds.

set(GLIDE_VERSION_MAJOR 0)
set(GLIDE_VERSION_MINOR 0)
set(GLIDE_VERSION_PATCH 0)
set(GLIDE_VERSION_PRERELEASE "")
set(GLIDE_VERSION_BASE "0.0.0-unknown")

set(_version_file "${SOURCE_DIR}/VERSION")
if(EXISTS "${_version_file}")
    file(READ "${_version_file}" _raw_ver)
    string(STRIP "${_raw_ver}" _raw_ver)
    if(_raw_ver MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9A-Za-z.-]+))?$")
        set(GLIDE_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(GLIDE_VERSION_MINOR "${CMAKE_MATCH_2}")
        set(GLIDE_VERSION_PATCH "${CMAKE_MATCH_3}")
        if(CMAKE_MATCH_5)
            set(GLIDE_VERSION_PRERELEASE "${CMAKE_MATCH_5}")
        endif()
        set(GLIDE_VERSION_BASE "${_raw_ver}")
    else()
        message(FATAL_ERROR "Invalid SemVer in ${_version_file}: '${_raw_ver}'")
    endif()
else()
    message(FATAL_ERROR "Missing root VERSION file at ${_version_file}")
endif()

set(GIT_HASH "unknown")
set(GIT_BRANCH "unknown")
set(_is_dirty FALSE)
set(_exact_tag "")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE _git_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_hash_rc
    )
    if(_git_hash_rc EQUAL 0 AND _git_hash)
        set(GIT_HASH "${_git_hash}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE _git_dirty
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_git_dirty)
            set(_is_dirty TRUE)
            set(GIT_HASH "${GIT_HASH}-dirty")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match HEAD
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE _tag_out
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _tag_rc
        )
        if(_tag_rc EQUAL 0 AND _tag_out)
            set(_exact_tag "${_tag_out}")
        endif()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE _git_branch
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_branch_rc
    )
    if(_git_branch_rc EQUAL 0 AND _git_branch)
        set(GIT_BRANCH "${_git_branch}")
    endif()
endif()

# Compose tag-aware SemVer build string:
# - Exact clean release tag `v<VERSION_BASE>` -> "<VERSION_BASE>"
# - Otherwise with git metadata               -> "<VERSION_BASE>+g<hash[-dirty]>"
# - Tarball build without git                 -> "<VERSION_BASE>"
if(NOT _is_dirty AND _exact_tag STREQUAL "v${GLIDE_VERSION_BASE}")
    set(GLIDE_VERSION_STRING "${GLIDE_VERSION_BASE}")
elseif(NOT GIT_HASH STREQUAL "unknown")
    set(GLIDE_VERSION_STRING "${GLIDE_VERSION_BASE}+g${GIT_HASH}")
else()
    set(GLIDE_VERSION_STRING "${GLIDE_VERSION_BASE}")
endif()

if(NOT BUILD_TYPE)
    set(BUILD_TYPE "Unspecified")
endif()

set(_new_content
"#pragma once
#define GLIDE_VERSION_MAJOR ${GLIDE_VERSION_MAJOR}
#define GLIDE_VERSION_MINOR ${GLIDE_VERSION_MINOR}
#define GLIDE_VERSION_PATCH ${GLIDE_VERSION_PATCH}
#define GLIDE_VERSION_PRERELEASE \"${GLIDE_VERSION_PRERELEASE}\"
#define GLIDE_VERSION_BASE \"${GLIDE_VERSION_BASE}\"
#define GLIDE_VERSION_STRING \"${GLIDE_VERSION_STRING}\"
#define GLIDE_GIT_HASH \"${GIT_HASH}\"
#define GLIDE_GIT_BRANCH \"${GIT_BRANCH}\"
#define GLIDE_BUILD_TYPE \"${BUILD_TYPE}\"
")

set(_old_content "")
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" _old_content)
endif()

if(NOT _old_content STREQUAL _new_content)
    get_filename_component(_out_dir "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_out_dir}")
    file(WRITE "${OUTPUT_FILE}" "${_new_content}")
endif()
