include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(ext_coex SRCS ex/main.c PRIV_REQUIRES nvs_flash esp_event)
