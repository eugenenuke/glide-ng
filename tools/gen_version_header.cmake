set(GIT_HASH "unknown")
set(GIT_BRANCH "unknown")

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
            set(GIT_HASH "${GIT_HASH}-dirty")
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

if(NOT BUILD_TYPE)
    set(BUILD_TYPE "Unspecified")
endif()

set(_new_content
"#pragma once
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
