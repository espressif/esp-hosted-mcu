include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(bleprph_gatt SRCS ex/bleprph_gatt.c REQUIRES nvs_flash bt)
