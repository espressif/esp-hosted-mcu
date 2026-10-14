include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(wifi_sta SRCS ex/main.c PRIV_REQUIRES esp_wifi nvs_flash esp_event esp_netif RENAME wifi_init_sta)
