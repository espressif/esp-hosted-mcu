# Off-target eh_frame wire test. Builds common/eh_frame with the host compiler,
# once with the default if_type numbering and once with the legacy numbering.
#
#   cmake -S tests/unit/eh_frame -B build_unit && cmake --build build_unit && ctest --test-dir build_unit

cmake_minimum_required(VERSION 3.16)
project(eh_frame_unit C)

set(CMAKE_C_STANDARD 11)
set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

enable_testing()

foreach(variant default legacy)
    add_executable(test_eh_frame_${variant}
        test_eh_frame.c
        ${REPO}/common/eh_frame/src/eh_frame.c
    )
    target_include_directories(test_eh_frame_${variant} PRIVATE
        stub
        ${REPO}/common/eh_frame/include
        ${REPO}/common/eh_common/include
        ${REPO}/common/eh_tlv/include
    )
    target_compile_options(test_eh_frame_${variant} PRIVATE -Wall -Wextra -Werror)
    if(variant STREQUAL "legacy")
        target_compile_definitions(test_eh_frame_${variant} PRIVATE
            CONFIG_ESP_HOSTED_LEGACY_WIRE_INTERFACE_TYPES=1)
    endif()
    add_test(NAME eh_frame_${variant} COMMAND test_eh_frame_${variant})
endforeach()
