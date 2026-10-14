include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(get_cp_fw_version SRCS ex/main.c PRIV_REQUIRES nvs_flash esp_event)
