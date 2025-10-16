/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"

#include "rpc_wrap.h"
#include "port_esp_hosted_host_config.h"

#if H_ENABLE_GPIO_CONTROL
#include "esp_hosted_gpio.h"

esp_err_t esp_hosted_gpio_config(const esp_hosted_gpio_config_t *pGPIOConfig)
{
  return rpc_gpio_config(pGPIOConfig);
}

esp_err_t esp_hosted_gpio_reset_pin(uint32_t gpio_num)
{
  return rpc_gpio_reset_pin(gpio_num);
}

esp_err_t esp_hosted_gpio_set_level(uint32_t gpio_num, uint32_t level)
{
  return rpc_gpio_set_level(gpio_num, level);
}

int esp_hosted_gpio_get_level(uint32_t gpio_num)
{
  int level = 0;
  rpc_gpio_get_level(gpio_num, &level);
  return level;
}

esp_err_t esp_hosted_gpio_set_direction(uint32_t gpio_num, uint32_t mode)
{
  return rpc_gpio_set_direction(gpio_num, mode);
}

esp_err_t esp_hosted_gpio_input_enable(uint32_t gpio_num)
{
  return rpc_gpio_input_enable(gpio_num);
}

esp_err_t esp_hosted_gpio_set_pull_mode(uint32_t gpio_num, uint32_t pull)
{
  return rpc_gpio_set_pull_mode(gpio_num, pull);
}

#endif // H_ENABLE_GPIO_CONTROL
