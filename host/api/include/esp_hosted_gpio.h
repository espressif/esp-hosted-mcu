/* APIs to do control GPIOs of the co-processor
 *
 * Note: This API is platform dependent
 *
 * Source for the API should be in host/port/<platform>/...
 *
 */

#ifndef __ESP_HOSTED_GPIO_H__
#define __ESP_HOSTED_GPIO_H__


#include "esp_err.h"

esp_err_t esp_hosted_gpio_config(const gpio_config_t *pGPIOConfig);
esp_err_t esp_hosted_gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t esp_hosted_gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int esp_hosted_gpio_get_level(gpio_num_t gpio_num);
esp_err_t esp_hosted_gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t esp_hosted_gpio_input_enable(gpio_num_t gpio_num);
esp_err_t esp_hosted_gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull);



#endif /*__ESP_HOSTED_GPIO_H__*/
