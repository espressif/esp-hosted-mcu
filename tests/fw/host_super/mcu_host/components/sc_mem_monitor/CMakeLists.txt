include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(mem_monitor SRCS ex/main.c ex/station_example.c REQUIRES esp_event PRIV_REQUIRES nvs_flash esp_wifi esp_netif RENAME example_wifi_deinit_sta example_wifi_init_once example_wifi_init_sta example_wifi_sta_disconnect)
