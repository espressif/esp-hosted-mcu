include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(controller_mac_addr SRCS ex/main.c REQUIRES nvs_flash bt)
