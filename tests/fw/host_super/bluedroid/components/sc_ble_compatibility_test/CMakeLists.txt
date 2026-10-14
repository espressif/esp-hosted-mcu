include(${CMAKE_CURRENT_LIST_DIR}/../../scenario.cmake)
eh_scenario(ble_compatibility_test SRCS ex/ble_compatibility_test.c REQUIRES nvs_flash bt RENAME example_exec_write_event_env example_prepare_write_event_env gatt_db_handle_table long_write)
