if(NOT CONFIG_ESP_HOSTED_CP_FEAT_PHY_ANT_READY)
    return()
endif()

add_library(eh_cp_feat_phy_ant STATIC
    "src/eh_cp_feat_phy_ant.c"
)
target_include_directories(eh_cp_feat_phy_ant
    PUBLIC "include"
)
target_link_libraries(eh_cp_feat_phy_ant
    PUBLIC eh_cp_core eh_common idf::esp_common idf::esp_phy
)
if(CONFIG_ESP_HOSTED_CP_FEAT_PHY_ANT_AUTO_INIT)
    target_link_options(eh_cp_feat_phy_ant INTERFACE
    "-Wl,--undefined=eh_cp_feat_phy_ant_init")
endif()

if(TARGET eh_cp)
    target_link_libraries(eh_cp INTERFACE eh_cp_feat_phy_ant)
endif()
