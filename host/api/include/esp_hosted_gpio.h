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

typedef struct
{
  uint64_t pin_bit_mask;   /*!< GPIO pin: set with bit mask, each bit maps to a GPIO */
  uint32_t mode;           /*!< GPIO mode: set input/output mode                     */
  uint32_t pull_up_en;     /*!< GPIO pull-up                                         */
  uint32_t pull_down_en;   /*!< GPIO pull-down                                       */
  uint32_t intr_type;      /*!< GPIO interrupt type                                  */

} esp_hosted_gpio_config_t;

esp_err_t esp_hosted_gpio_config(const esp_hosted_gpio_config_t *pGPIOConfig);
esp_err_t esp_hosted_gpio_reset_pin(uint32_t gpio_num);
esp_err_t esp_hosted_gpio_set_level(uint32_t gpio_num, uint32_t level);
int esp_hosted_gpio_get_level(uint32_t gpio_num);
esp_err_t esp_hosted_gpio_set_direction(uint32_t gpio_num, uint32_t mode);
esp_err_t esp_hosted_gpio_input_enable(uint32_t gpio_num);
esp_err_t esp_hosted_gpio_set_pull_mode(uint32_t gpio_num, uint32_t pull);

#endif /*__ESP_HOSTED_GPIO_H__*/
