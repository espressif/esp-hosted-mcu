/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __H_SLAVE_GPIO_EXTENDER_H__
#define __H_SLAVE_GPIO_EXTENDER_H__
#include <stdbool.h>
#include "sdkconfig.h"

#ifdef CONFIG_ESP_HOSTED_ENABLE_GPIO_EXPANDER
    #define H_GPIO_EXPANDER_SUPPORT (1)
#else
    #define H_GPIO_EXPANDER_SUPPORT (0)
#endif

#if H_GPIO_EXPANDER_SUPPORT

#include "driver/gpio.h"

/**
 * @brief   Check if a GPIO pin is free for general use.
 *
 * @param   pin     GPIO number to test
 * @return  true    Pin is free (eligible for GPIO)
 *          false   Pin is reserved by a host transport interface
 */
uint8_t transport_gpio_pin_guard_is_eligible(gpio_num_t pin);

#endif


#endif
