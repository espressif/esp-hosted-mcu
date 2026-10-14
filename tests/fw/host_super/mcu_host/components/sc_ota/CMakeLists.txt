include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(ota SRCS ex/main.c REQUIRES console esp_timer PRIV_REQUIRES nvs_flash esp_event esp_app_format ota_https ota_littlefs ota_partition)
