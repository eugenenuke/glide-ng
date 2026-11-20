# Optional Open Watcom v2 DOS/32 (CauseWay) cross-build integration.
# Automatically detects vendor/watcom or $ENV{WATCOM}; never fails Linux builds
# when the Watcom toolchain is absent.

set(GLIDE_HAS_WATCOM_DOS FALSE)
set(GLIDE_WATCOM_ROOT "")
set(GLIDE_WATCOM_BIN_DIR "")

if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/watcom/binl64/wcc386")
    set(GLIDE_WATCOM_ROOT "${CMAKE_SOURCE_DIR}/vendor/watcom")
    set(GLIDE_WATCOM_BIN_DIR "${GLIDE_WATCOM_ROOT}/binl64")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/vendor/watcom/binl/wcc386")
    set(GLIDE_WATCOM_ROOT "${CMAKE_SOURCE_DIR}/vendor/watcom")
    set(GLIDE_WATCOM_BIN_DIR "${GLIDE_WATCOM_ROOT}/binl")
elseif(DEFINED ENV{WATCOM} AND EXISTS "$ENV{WATCOM}/binl64/wcc386")
    set(GLIDE_WATCOM_ROOT "$ENV{WATCOM}")
    set(GLIDE_WATCOM_BIN_DIR "${GLIDE_WATCOM_ROOT}/binl64")
elseif(DEFINED ENV{WATCOM} AND EXISTS "$ENV{WATCOM}/binl/wcc386")
    set(GLIDE_WATCOM_ROOT "$ENV{WATCOM}")
    set(GLIDE_WATCOM_BIN_DIR "${GLIDE_WATCOM_ROOT}/binl")
endif()

if(GLIDE_WATCOM_BIN_DIR AND EXISTS "${GLIDE_WATCOM_BIN_DIR}/wlink")
    set(GLIDE_HAS_WATCOM_DOS TRUE)
    set(GLIDE_WCC386 "${GLIDE_WATCOM_BIN_DIR}/wcc386")
    set(GLIDE_WLINK "${GLIDE_WATCOM_BIN_DIR}/wlink")
    set(GLIDE_DOS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/dos")
    file(MAKE_DIRECTORY "${GLIDE_DOS_OUTPUT_DIR}")
    message(STATUS "Open Watcom v2 detected at ${GLIDE_WATCOM_ROOT}; enabling DOS/32 CauseWay tools -> ${GLIDE_DOS_OUTPUT_DIR}")
    if(NOT TARGET dos_tools)
        add_custom_target(dos_tools ALL COMMENT "Building DOS/32 CauseWay Glide tools")
    endif()
else()
    message(STATUS "Open Watcom v2 not found (checked vendor/watcom and $WATCOM); skipping optional DOS/32 .exe cross-builds")
endif()

# Build a self-contained DOS/32 CauseWay executable linked dynamically to GLIDE2X.OVL.
# Usage:
#   add_dos_glide_executable(glide_info
#       SOURCES tools/glide_info/glide_info.c
#       INCLUDES include include/glide2 include/3dfx tools/common
#   )
function(add_dos_glide_executable TARGET_NAME)
    if(NOT GLIDE_HAS_WATCOM_DOS)
        return()
    endif()

    cmake_parse_arguments(DOS "" "" "SOURCES;INCLUDES;DEFINES;DEPENDS" ${ARGN})

    set(_obj_dir "${CMAKE_CURRENT_BINARY_DIR}/dos_obj_${TARGET_NAME}")
    file(MAKE_DIRECTORY "${_obj_dir}")

    set(_inc_flags "-i=${GLIDE_WATCOM_ROOT}/h")
    foreach(_inc ${DOS_INCLUDES})
        list(APPEND _inc_flags "-i=${_inc}")
    endforeach()

    set(_def_flags "-dGLIDE_VERSION=2")
    foreach(_def ${DOS_DEFINES})
        list(APPEND _def_flags "-d${_def}")
    endforeach()

    set(_obj_files "")
    set(_link_file_args "")
    foreach(_src ${DOS_SOURCES})
        get_filename_component(_abs_src "${_src}" ABSOLUTE)
        get_filename_component(_src_name "${_abs_src}" NAME_WE)
        set(_obj "${_obj_dir}/${_src_name}.obj")
        list(APPEND _obj_files "${_obj}")
        list(APPEND _link_file_args "file" "${_obj}")

        add_custom_command(
            OUTPUT "${_obj}"
            COMMAND "${CMAKE_COMMAND}" -E env
                "WATCOM=${GLIDE_WATCOM_ROOT}"
                "INCLUDE=${GLIDE_WATCOM_ROOT}/h"
                "PATH=${GLIDE_WATCOM_BIN_DIR}:$ENV{PATH}"
                "${GLIDE_WCC386}"
                -bt=dos -3s -fpi87 -ox -za99 -wx -zq
                ${_def_flags}
                ${_inc_flags}
                "-fo=${_obj}"
                "${_abs_src}"
            DEPENDS "${_abs_src}" ${DOS_DEPENDS}
            COMMENT "Watcom wcc386 (DOS/32): ${_src_name}.c -> ${_src_name}.obj"
            VERBATIM
        )
    endforeach()

    set(_exe_out "${GLIDE_DOS_OUTPUT_DIR}/${TARGET_NAME}.exe")
    set(_cwstub_arg "")
    if(EXISTS "${GLIDE_WATCOM_ROOT}/binw/cwstub.exe")
        set(_cwstub_arg "option" "stub=${GLIDE_WATCOM_ROOT}/binw/cwstub.exe")
    elseif(EXISTS "${GLIDE_WATCOM_ROOT}/binl/cwstub.exe")
        set(_cwstub_arg "option" "stub=${GLIDE_WATCOM_ROOT}/binl/cwstub.exe")
    endif()
    add_custom_command(
        OUTPUT "${_exe_out}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "WATCOM=${GLIDE_WATCOM_ROOT}"
            "PATH=${GLIDE_WATCOM_BIN_DIR}:${GLIDE_WATCOM_ROOT}/binw:$ENV{PATH}"
            "${GLIDE_WLINK}"
            option quiet
            system causeway
            ${_cwstub_arg}
            name "${_exe_out}"
            option stack=65536
            ${_link_file_args}
        DEPENDS ${_obj_files}
        COMMENT "Watcom wlink (CauseWay): ${_exe_out}"
        VERBATIM
    )

    add_custom_target("dos_${TARGET_NAME}" DEPENDS "${_exe_out}")
    add_dependencies(dos_tools "dos_${TARGET_NAME}")
endfunction()

# Build a freestanding DOS/32 LE DLL overlay (e.g. GLIDE2X.OVL) via Open Watcom.
# Usage:
#   add_dos_glide_ovl(glide2x
#       SOURCES src/dos_ovl/glide2x_ovl.c
#       LINKER_SCRIPT src/dos_ovl/glide2x_ovl.lnk
#   )
function(add_dos_glide_ovl TARGET_NAME)
    if(NOT GLIDE_HAS_WATCOM_DOS)
        return()
    endif()

    cmake_parse_arguments(OVL "" "LINKER_SCRIPT" "SOURCES;INCLUDES;DEFINES;DEPENDS" ${ARGN})

    set(_obj_dir "${CMAKE_CURRENT_BINARY_DIR}/dos_ovl_${TARGET_NAME}")
    file(MAKE_DIRECTORY "${_obj_dir}")

    set(_inc_flags "-i=${GLIDE_WATCOM_ROOT}/h")
    foreach(_inc ${OVL_INCLUDES})
        list(APPEND _inc_flags "-i=${_inc}")
    endforeach()

    set(_def_flags "")
    foreach(_def ${OVL_DEFINES})
        list(APPEND _def_flags "-d${_def}")
    endforeach()

    set(_obj_files "")
    set(_link_file_args "")
    foreach(_src ${OVL_SOURCES})
        get_filename_component(_abs_src "${_src}" ABSOLUTE)
        get_filename_component(_src_name "${_abs_src}" NAME_WE)
        set(_obj "${_obj_dir}/${_src_name}.obj")
        list(APPEND _obj_files "${_obj}")
        list(APPEND _link_file_args "file" "${_obj}")

        add_custom_command(
            OUTPUT "${_obj}"
            COMMAND "${CMAKE_COMMAND}" -E env
                "WATCOM=${GLIDE_WATCOM_ROOT}"
                "INCLUDE=${GLIDE_WATCOM_ROOT}/h"
                "PATH=${GLIDE_WATCOM_BIN_DIR}:$ENV{PATH}"
                "${GLIDE_WCC386}"
                -bt=dos -bd -3s -fpi87 -ox -za99 -wx -zq -zl
                ${_def_flags}
                ${_inc_flags}
                "-fo=${_obj}"
                "${_abs_src}"
            DEPENDS "${_abs_src}" ${OVL_DEPENDS}
            COMMENT "Watcom wcc386 (DOS/32 LE DLL): ${_src_name}.c -> ${_src_name}.obj"
            VERBATIM
        )
    endforeach()

    get_filename_component(_abs_lnk "${OVL_LINKER_SCRIPT}" ABSOLUTE)
    set(_ovl_out "${GLIDE_DOS_OUTPUT_DIR}/${TARGET_NAME}.ovl")

    add_custom_command(
        OUTPUT "${_ovl_out}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "WATCOM=${GLIDE_WATCOM_ROOT}"
            "PATH=${GLIDE_WATCOM_BIN_DIR}:$ENV{PATH}"
            "${GLIDE_WLINK}"
            "@${_abs_lnk}"
            name "${_ovl_out}"
            ${_link_file_args}
        DEPENDS ${_obj_files} "${_abs_lnk}"
        COMMENT "Watcom wlink (DOS/32 LE OVL): ${_ovl_out}"
        VERBATIM
    )

    add_custom_target("dos_ovl_${TARGET_NAME}" DEPENDS "${_ovl_out}")
    add_dependencies(dos_tools "dos_ovl_${TARGET_NAME}")
endfunction()
