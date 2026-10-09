if(NOT CONFIG_ESP_HOSTED_CP_FEAT_RF_CERT_READY)
    return()
endif()

add_library(eh_cp_feat_rf_cert STATIC
    "src/eh_cp_feat_rf_cert.c"
)
target_include_directories(eh_cp_feat_rf_cert
    PUBLIC "include"
)
target_link_libraries(eh_cp_feat_rf_cert
    PUBLIC eh_cp_core eh_common idf::esp_common idf::esp_phy idf::esp_wifi
)
if(CONFIG_ESP_HOSTED_CP_FEAT_RF_CERT_AUTO_INIT)
    target_link_options(eh_cp_feat_rf_cert INTERFACE
    "-Wl,--undefined=eh_cp_feat_rf_cert_init")
endif()

if(TARGET eh_cp)
    target_link_libraries(eh_cp INTERFACE eh_cp_feat_rf_cert)
endif()
