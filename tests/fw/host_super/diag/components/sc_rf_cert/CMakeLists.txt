include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(rf_cert SRCS ex/host_rf_cert_example.c ex/rf_cert_ota_cmd.c REQUIRES rf_cert_ota PRIV_REQUIRES nvs_flash esp_event console)
