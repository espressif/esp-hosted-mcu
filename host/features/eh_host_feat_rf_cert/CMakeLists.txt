# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_ESP_HOSTED_HOST_FEAT_RF_CERT_READY)
    return()
endif()

add_library(eh_host_feat_rf_cert STATIC
    "src/eh_host_feat_rf_cert.c"
)

# esp_phy registers with no include dirs on a radio-less host (esp32p4,
# linux), so the cert-test header is taken from the IDF tree directly.  It is
# #pragma once, so a host that does get esp_phy's own include dir is fine.
target_include_directories(eh_host_feat_rf_cert
    PUBLIC "include"
        "$ENV{IDF_PATH}/components/esp_phy/include"
    PRIVATE "src"
)

target_link_libraries(eh_host_feat_rf_cert
    PUBLIC
        eh_host_port_api
        eh_common
)

if(CONFIG_ESP_HOSTED_HOST_FEAT_RF_CERT_AUTO_INIT)
    target_link_options(eh_host_feat_rf_cert INTERFACE
        "-Wl,--undefined=eh_host_feat_rf_cert_init")
endif()

target_link_libraries(eh_host_feat_rf_cert PRIVATE eh_host_autoinit)

target_sources(eh_host_feat_rf_cert PRIVATE
    "src/eh_host_phy_cert.c"
    "src/eh_host_feat_rf_cert_v2_req.c"
    "src/eh_host_feat_rf_cert_v2_resp_evt.c"
)
target_link_libraries(eh_host_feat_rf_cert PRIVATE
    eh_host_feat_rpc_ext_v2
    eh_host_feat_rpc
    eh_proto_v2
)
