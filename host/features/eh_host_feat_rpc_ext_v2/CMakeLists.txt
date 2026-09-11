# SPDX-License-Identifier: Apache-2.0
# eh_host_feat_rpc_ext_v2 — MCU V1 Rpc mappings.

if(NOT CONFIG_ESP_HOSTED_HOST_FEAT_RPC_EXT_V2_READY)
    return()
endif()

set(SRCS
    "src/eh_host_feat_rpc_ext_v2.c"
    "src/eh_host_feat_rpc_ext_v2_pack.c"
    "src/eh_host_feat_rpc_ext_v2_decode.c"
    "src/eh_host_feat_rpc_ext_v2_api_common.c"
)

# Shared RpcReqFeatureControl wire op — used by every feature whose lifecycle
# rides Req_FeatureControl.  Both files compile to nothing unless
# EH_HOST_FEAT_FEATURE_CONTROL_READY is set, so that one gate in
# rpc_ext_v2_priv.h is the only place the feature list lives.
list(APPEND SRCS "src/feature_control_req.c")
list(APPEND SRCS "src/feature_control_resp_evt.c")

add_library(eh_host_feat_rpc_ext_v2 STATIC ${SRCS})

target_include_directories(eh_host_feat_rpc_ext_v2
    PUBLIC "include"
    PRIVATE "src"
)

target_link_libraries(eh_host_feat_rpc_ext_v2
    PUBLIC
        eh_host_feat_rpc
        eh_proto_v2
        eh_host_port_api
        eh_common
        eh_frame
        eh_tlv
    PRIVATE
        eh_host_config
)

# Per-feature PRIVATE links are wired in host/CMakeLists.txt AFTER feature
# targets exist; static-lib cycle is handled via --start-group on final link.
