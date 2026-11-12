# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_ESP_HOSTED_HOST_FEAT_PHY_ANT_READY)
    return()
endif()

add_library(eh_host_feat_phy_ant STATIC
    "src/eh_host_phy_ant.c"
    "src/eh_host_feat_phy_ant_v2_req.c"
    "src/eh_host_feat_phy_ant_v2_resp_evt.c"
)

target_include_directories(eh_host_feat_phy_ant
    PUBLIC "include"
)

target_link_libraries(eh_host_feat_phy_ant
    PUBLIC
        eh_host_port_api
        eh_common
    PRIVATE
        eh_host_feat_rpc_ext_v2
        eh_host_feat_rpc
        eh_proto_v2
)

if(CONFIG_ESP_HOSTED_HOST_FEAT_PHY_ANT_AUTO_INIT)
    target_link_options(eh_host_feat_phy_ant INTERFACE
        "-Wl,--undefined=eh_host_feat_phy_ant_init")
endif()

if(CONFIG_ESP_HOSTED_HOST_AUTO_FEAT_INIT)
    target_link_libraries(eh_host_feat_phy_ant PRIVATE eh_host_autoinit)
endif()
