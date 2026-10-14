include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(hosted_events SRCS ex/main.c ex/station_example.c PRIV_REQUIRES esp_wifi nvs_flash esp_event esp_netif esp_timer RENAME example_wifi_abort_wait example_wifi_deinit_sta example_wifi_init_once example_wifi_init_sta example_wifi_sta_netif_close my_timer_cb)
