include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(peer_data_transfer SRCS ex/main.c PRIV_REQUIRES esp_wifi nvs_flash esp_event)
