/* Current GPIO method(s) supported:
 * - gpio_config()
 * - gpio_reset_pin()
 * - gpio_set_level()
 * - gpio_get_level()
 * - gpio_set_direction()
 * - gpio_input_enable()
 * - gpio_set_pull_mode()
 */

#include "esp_log.h"

#include "rpc_wrap.h"
#include "esp_hosted_gpio.h"

static char *TAG = "hosted_gpio";

esp_err_t esp_hosted_gpio_config(const gpio_config_t *pGPIOConfig)
{
  return rpc_gpio_config(pGPIOConfig);
}

esp_err_t esp_hosted_gpio_reset_pin(gpio_num_t gpio_num)
{
  return rpc_gpio_reset_pin(gpio_num);
}

esp_err_t esp_hosted_gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
  return rpc_gpio_set_level(gpio_num, level);
}

int esp_hosted_gpio_get_level(gpio_num_t gpio_num)
{
  int level = 0;
  esp_err_t err = rpc_gpio_get_level(gpio_num, &level);
  return level;
}

esp_err_t esp_hosted_gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
  return rpc_gpio_set_direction(gpio_num, mode);
}

esp_err_t esp_hosted_gpio_input_enable(gpio_num_t gpio_num)
{
  return rpc_gpio_input_enable(gpio_num);
}

esp_err_t esp_hosted_gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
  return rpc_gpio_set_pull_mode(gpio_num, pull);
}
