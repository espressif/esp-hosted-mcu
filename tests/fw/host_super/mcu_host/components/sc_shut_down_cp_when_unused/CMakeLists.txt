include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(shut_down_cp_when_unused SRCS ex/main.c ex/memory_debug.c PRIV_REQUIRES esp_wifi nvs_flash esp_event esp_driver_gpio RENAME memory_debug_end_cycle memory_debug_init memory_debug_log_heap memory_debug_start_cycle power_save_task)
