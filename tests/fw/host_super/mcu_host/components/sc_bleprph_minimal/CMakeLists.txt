include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(bleprph_minimal SRCS ex/bleprph_minimal.c REQUIRES nvs_flash bt)
