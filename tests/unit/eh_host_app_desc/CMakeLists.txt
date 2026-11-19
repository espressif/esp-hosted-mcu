# Off-target test of the host AppGetDesc response parser.
#
#   cmake -S tests/unit/eh_host_app_desc -B build_unit && cmake --build build_unit && ctest --test-dir build_unit

cmake_minimum_required(VERSION 3.16)
project(eh_host_app_desc_unit C)

set(CMAKE_C_STANDARD 11)
set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

enable_testing()

add_executable(test_eh_host_app_desc
    test_eh_host_app_desc.c
    ${REPO}/host/features/eh_host_feat_system/src/eh_host_feat_system_v2_resp_evt.c
)
target_include_directories(test_eh_host_app_desc PRIVATE
    stub
    ${REPO}/host/features/eh_host_feat_rpc_ext_v2/include
    ${REPO}/host/features/eh_host_feat_rpc/include
    ${REPO}/common/serializers/eh_proto_v2/include
    ${REPO}/common/serializers/third_party/msg_codec/protobuf-c
    ${REPO}/common/eh_common/include
    ${REPO}/common/eh_tlv/include
)
target_compile_options(test_eh_host_app_desc PRIVATE -Wall -Wextra -Werror)
add_test(NAME eh_host_app_desc COMMAND test_eh_host_app_desc)
