if(NOT CONFIG_ESP_HOSTED_CP_FEAT_WIFI_READY)
    return()
endif()

set(srcs
    "src/eh_cp_feat_wifi.c"
    "src/eh_cp_feat_wifi_init.c"
    "src/eh_cp_feat_wifi_event_publisher.c"
    "src/eh_cp_feat_wifi_sta_fsm.c"
    "src/eh_cp_feat_wifi_ap_fsm.c"
)

add_library(eh_cp_feat_wifi STATIC ${srcs})
target_include_directories(eh_cp_feat_wifi
    PUBLIC
        "include"
)
target_link_libraries(eh_cp_feat_wifi
    PUBLIC
        eh_cp_core
        eh_common
        idf::esp_common
        idf::esp_event
        idf::esp_wifi
)
target_link_options(eh_cp_feat_wifi INTERFACE
    "-Wl,--wrap=esp_wifi_connect"
    "-Wl,--wrap=esp_wifi_internal_reg_rxcb"
    "-Wl,--wrap=esp_wifi_init")
if(CONFIG_ESP_HOSTED_CP_FEAT_WIFI_AUTO_INIT)
    target_link_options(eh_cp_feat_wifi INTERFACE
        "-Wl,--undefined=eh_cp_feat_wifi_init")
endif()
if(CONFIG_ESP_HOSTED_CP_FEAT_WIFI_EXT_ENT_READY)
    target_link_libraries(eh_cp_feat_wifi PUBLIC eh_cp_feat_wifi_ext_ent)
endif()
if(CONFIG_ESP_HOSTED_CP_FEAT_WIFI_EXT_ITWT_READY)
    target_link_libraries(eh_cp_feat_wifi PUBLIC eh_cp_feat_wifi_ext_itwt)
endif()
if(CONFIG_ESP_HOSTED_CP_FEAT_WIFI_EXT_DPP_READY)
    target_link_libraries(eh_cp_feat_wifi PUBLIC eh_cp_feat_wifi_ext_dpp)
endif()

if(TARGET eh_cp)
    target_link_libraries(eh_cp INTERFACE eh_cp_feat_wifi)
endif()
