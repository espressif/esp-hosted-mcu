# ESP-Hosted GPIO Control Example

This example demonstrates how to use the ESP-Hosted solution to control the GPIOs of the slave (co-processor) from the host MCU.

## How to Use

### Prerequisites

1.  **Hardware:**
    *   An ESP32-based board to act as the host MCU.
    *   An ESP32-based board to act as the slave/co-processor.
    *   The two boards should be connected via one of the supported transport interfaces (SPI, SDIO, or UART).

2.  **Slave Configuration:**
    *   The firmware flashed on the slave board must have the GPIO RPC feature enabled. In the slave's `menuconfig`, set the following:
        ```
        Component config --->
            ESP-Hosted --->
                [*] Enable GPIO Expander feature on host
        ```
        This corresponds to the `CONFIG_ESP_HOSTED_ENABLE_GPIO_EXPANDER=y` option.

### Build and Flash

1.  **Host:**
    *   Navigate to this example directory: `cd examples/host_gpio_expander`.
    *   Set your target board (e.g., `idf.py set-target esp32`).
    *   Build and flash the project: `idf.py build flash`.
    *   Monitor the output: `idf.py monitor`.

The host application will:
1.  Initialize the ESP-Hosted transport.
2.  Configure a specific GPIO pin on the slave as an output.
3.  Toggle the GPIO level (HIGH and LOW) every second.
4.  After a few toggles, it will reconfigure the same GPIO as an input.
5.  Read and print the GPIO level.

You can connect an LED to the specified GPIO on the slave board to visually verify the output functionality.
