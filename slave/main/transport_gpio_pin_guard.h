#pragma once

#include "driver/gpio.h"
#include <stdbool.h>

/**
 * @brief   Check if a GPIO pin is free for general use.
 *
 * @param   pin     GPIO number to test
 * @return  true    Pin is free (eligible for GPIO)
 *          false   Pin is reserved by a host transport interface
 */
uint8_t transport_gpio_pin_guard_is_eligible(gpio_num_t pin);

