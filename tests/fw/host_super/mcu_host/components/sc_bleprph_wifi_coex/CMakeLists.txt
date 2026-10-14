include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(bleprph_wifi_coex SRCS ex/main.c ex/gatt_svr.c REQUIRES nvs_flash bt esp_hosted_examples_common PRIV_REQUIRES esp_wifi esp_event esp_netif RENAME bleprph_host_task gatt_svr_init gatt_svr_register_cb)
