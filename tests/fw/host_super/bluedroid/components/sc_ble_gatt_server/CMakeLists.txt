include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(ble_gatt_server SRCS ex/ble_gatt_server.c REQUIRES nvs_flash bt)
