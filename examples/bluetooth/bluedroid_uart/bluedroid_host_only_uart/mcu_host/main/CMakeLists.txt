idf_component_register(SRCS "main.c"
                            "uart_driver.c"
                    PRIV_REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio
                    INCLUDE_DIRS ".")
