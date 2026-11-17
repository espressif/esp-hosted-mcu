# SPDX-License-Identifier: Apache-2.0
#
# Stops the configure step early when the checkout cannot build.
# Runs in ESP-IDF's requirement-scan (script mode) too, so the message
# replaces IDF's "Failed to resolve component 'esp_hosted'".

include_guard(GLOBAL)

# Prints the header (yellow, unless NO_COLOR is set) and the lines in an
# ASCII box. IDF re-wraps the error text of the script-mode scan and
# multiplies blank lines; indented lines with no blank lines between
# them pass through unchanged.
function(_eh_checkout_fail _header)
    list(PREPEND ARGN "${_header}")
    set(_w 0)
    foreach(_line IN LISTS ARGN)
        string(LENGTH "${_line}" _n)
        if(_n GREATER _w)
            set(_w ${_n})
        endif()
    endforeach()
    string(REPEAT "-" ${_w} _rule)
    set(_msg "\n  +-${_rule}-+")
    foreach(_line IN LISTS ARGN)
        string(LENGTH "${_line}" _n)
        math(EXPR _pad "${_w} - ${_n}")
        string(REPEAT " " ${_pad} _sp)
        if(_line STREQUAL _header AND NOT DEFINED ENV{NO_COLOR})
            string(ASCII 27 _esc)
            set(_line "${_esc}[1;33m${_line}${_esc}[0m")
        endif()
        string(APPEND _msg "\n  | ${_line}${_sp} |")
    endforeach()
    message(FATAL_ERROR "${_msg}\n  +-${_rule}-+\n")
endfunction()

# Build directory of the project being configured. A failed configure
# leaves it without CMakeCache.txt, so `idf.py fullclean` refuses it.
set(_eh_build_dir "${CMAKE_BINARY_DIR}")
if(COMMAND idf_build_get_property)
    idf_build_get_property(_eh_build_dir BUILD_DIR)
endif()

set(_eh_clone_cmd
    "git clone --recurse-submodules https://github.com/espressif/esp-hosted-mcu.git esp_hosted")

# IDF names a component after its directory; examples REQUIRE esp_hosted.
# <namespace>__esp_hosted is the name of a component-registry download.
if(ESP_PLATFORM)
    get_filename_component(_eh_dir_name "${EH_PATH}" NAME)
    if(NOT _eh_dir_name MATCHES "^([A-Za-z0-9_.-]+__)?esp_hosted$")
        get_filename_component(_eh_parent "${EH_PATH}" DIRECTORY)
        # The rename moves an example inside the repo; give its new path.
        set(_eh_proj "")
        if(COMMAND idf_build_get_property)
            idf_build_get_property(_eh_proj PROJECT_DIR)
        endif()
        # Windows paths compare without case (C:/ vs c:/).
        string(LENGTH "${EH_PATH}" _eh_len)
        foreach(_v _eh_proj _eh_build_dir)
            set(_a "${${_v}}/")
            set(_b "${EH_PATH}/")
            if(CMAKE_HOST_WIN32)
                string(TOLOWER "${_a}" _a)
                string(TOLOWER "${_b}" _b)
            endif()
            string(FIND "${_a}" "${_b}" _eh_at)
            if(_eh_at EQUAL 0)
                string(SUBSTRING "${${_v}}" ${_eh_len} -1 _rest)
                set(${_v} "${_eh_parent}/esp_hosted${_rest}")
            endif()
        endforeach()
        file(TO_NATIVE_PATH "${_eh_build_dir}" _eh_build_n)
        file(TO_NATIVE_PATH "${_eh_parent}" _eh_parent_n)
        if(_eh_proj)
            file(TO_NATIVE_PATH "${_eh_proj}" _eh_proj)
            set(_eh_cd_proj "cd \"${_eh_proj}\"")
            cmake_path(APPEND _eh_dependencies_path ${_eh_proj} "dependencies.lock")
            file(TO_NATIVE_PATH "${_eh_dependencies_path}" _eh_dependencies_path)
        else()
            set(_eh_cd_proj "Go back to the example directory.")
            set(_eh_dependencies_path "dependencies.lock")
        endif()
        _eh_checkout_fail(
            "ESP-Hosted checkout check 1 of 2 failed: directory name"
            "Rename the repository directory to \"esp_hosted\"."
            "ESP-IDF uses the directory name as the component name."
            "  1. cd \"${_eh_parent_n}\""
            "  2. cmake -E rename \"${_eh_dir_name}\" esp_hosted"
            "  3. ${_eh_cd_proj}"
            "  4. cmake -E rm \"${_eh_dependencies_path}\"   (can fail if file does not exist)"
            "  5. cmake -E rm -rf \"${_eh_build_n}\""
            "  6. Run the build command again.")
    endif()
endif()

if(EXISTS "${EH_PATH}/.gitmodules")
    file(STRINGS "${EH_PATH}/.gitmodules" _eh_sm_lines REGEX "^[ \t]*path[ \t]*=")
    set(_eh_sm_missing "")
    foreach(_l IN LISTS _eh_sm_lines)
        string(REGEX REPLACE "^[ \t]*path[ \t]*=[ \t]*" "" _p "${_l}")
        string(STRIP "${_p}" _p)
        file(GLOB _c LIST_DIRECTORIES true "${EH_PATH}/${_p}/*")
        if(NOT _c)
            list(APPEND _eh_sm_missing "  - ${_p}")
        endif()
    endforeach()
    if(_eh_sm_missing)
        file(TO_NATIVE_PATH "${EH_PATH}" _eh_src)
        file(TO_NATIVE_PATH "${_eh_build_dir}" _eh_build_n)
        cmake_path(APPEND _eh_dependencies_path ${_eh_src} "dependencies.lock")
        file(TO_NATIVE_PATH "${_eh_dependencies_path}" _eh_dependencies_path)
        if(EXISTS "${EH_PATH}/.git")
            set(_eh_sm_fix
                "  1. git -C \"${_eh_src}\" submodule update --init --recursive"
                "  2. cmake -E rm \"${_eh_dependencies_path}\"   (can fail if file does not exist)"
                "  3. cmake -E rm -rf \"${_eh_build_n}\""
                "  4. Run the build command again.")
        else()
            set(_eh_sm_fix
                "A ZIP download does not include submodules. Clone the repository:"
                "  ${_eh_clone_cmd}")
        endif()
        _eh_checkout_fail(
            "ESP-Hosted checkout check 2 of 2 failed: submodules"
            "These submodules are empty:"
            ${_eh_sm_missing}
            ${_eh_sm_fix})
    endif()
endif()
