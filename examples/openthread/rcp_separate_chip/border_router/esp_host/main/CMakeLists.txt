idf_component_register(SRCS "esp_ot_br.c" "app_demo.c" "ble_prph.c" "gatt_svr.c"
                       PRIV_REQUIRES console esp_driver_gpio esp_hosted esp_psram openthread esp_wifi nvs_flash bt
                       INCLUDE_DIRS ".")
