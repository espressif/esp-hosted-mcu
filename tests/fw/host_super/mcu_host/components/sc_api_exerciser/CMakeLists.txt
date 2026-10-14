include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(api_exerciser SRCS ex/main.c common/eh_api_cmd.c INCLUDE_DIRS common REQUIRES console esp_wifi PRIV_REQUIRES nvs_flash esp_event esp_netif RENAME eh_api_cmd_register_all)
