/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "slave_gpio_expander.h"
#include "slave_transport_gpio_pin_guard.h"
#include "slave_control.h"
#include "esp_log.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#if H_GPIO_EXPANDER_SUPPORT

static const char* TAG = "slave_gpio_expander";

#define GPIO_EXPANDER_ISR_QUEUE_LEN 10

typedef struct {
    int gpio_num;
    int level;
} gpio_expander_isr_evt_t;

static QueueHandle_t s_gpio_isr_queue = NULL;
static TaskHandle_t s_gpio_isr_task = NULL;
static bool s_gpio_isr_service_installed = false;
static bool s_gpio_isr_consent[GPIO_NUM_MAX] = { 0 };

static void gpio_expander_isr_task(void *arg)
{
    gpio_expander_isr_evt_t evt;
    while (1) {
        if (xQueueReceive(s_gpio_isr_queue, &evt, portMAX_DELAY)) {
            send_event_data_to_host(RPC_ID__Event_GpioInterrupt,
                    &evt, sizeof(evt));
        }
    }
}

static void gpio_expander_ensure_isr_task(void)
{
    if (!s_gpio_isr_queue) {
        s_gpio_isr_queue = xQueueCreate(GPIO_EXPANDER_ISR_QUEUE_LEN,
                sizeof(gpio_expander_isr_evt_t));
    }
    if (!s_gpio_isr_task) {
        xTaskCreate(gpio_expander_isr_task, "gpio_exp_evt",
                2048, NULL, tskIDLE_PRIORITY + 1, &s_gpio_isr_task);
    }
}

static bool gpio_expander_isr_allowed(int gpio_num, bool allow_disable)
{
    if (!transport_gpio_pin_guard_is_eligible((gpio_num_t)gpio_num)) {
        return false;
    }
    if (allow_disable) {
        return true;
    }
    return s_gpio_isr_consent[gpio_num];
}

static void gpio_expander_isr_handler(void *arg)
{
    int gpio_num = (int)(uintptr_t)arg;
    gpio_expander_isr_evt_t evt = {
        .gpio_num = gpio_num,
        .level = gpio_get_level(gpio_num),
    };

    if (s_gpio_isr_queue) {
        BaseType_t woke = pdFALSE;
        xQueueSendFromISR(s_gpio_isr_queue, &evt, &woke);
        if (woke) {
            portYIELD_FROM_ISR();
        }
    }
}

esp_err_t req_gpio_config(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioConfig, resp_gpio_config,
			RpcReqGpioConfig, req_gpio_config,
			rpc__resp__gpio_config__init);

	gpio_config_t config = {0};

	config.mode = req_payload->config->mode;
	config.pull_up_en = req_payload->config->pull_up_en;
	config.pull_down_en = req_payload->config->pull_down_en;
	config.intr_type = req_payload->config->intr_type;
	config.pin_bit_mask = req_payload->config->pin_bit_mask;

	bool isr_requested = (config.intr_type != GPIO_INTR_DISABLE);
	if (isr_requested && !req_payload->config->hosted_isr_enable) {
		ESP_LOGE(TAG, "ISR consent not provided by host");
		resp_payload->resp = ESP_ERR_NOT_SUPPORTED;
		return ESP_OK;
	}

	for (int pin = 0; pin < GPIO_NUM_MAX; ++pin) {
		if (config.pin_bit_mask & (1ULL << pin)) {
			if (!transport_gpio_pin_guard_is_eligible((gpio_num_t)pin)) {
				ESP_LOGE(TAG, "GPIO pin %d is not allowed to be configured", pin);
				resp_payload->resp = ESP_ERR_INVALID_ARG;
				return ESP_OK;
			}
			if (req_payload->config->hosted_isr_enable) {
				s_gpio_isr_consent[pin] = true;
			} else {
				s_gpio_isr_consent[pin] = false;
			}
		}
	}

	RPC_RET_FAIL_IF(gpio_config(&config));

	if (isr_requested) {
		gpio_expander_ensure_isr_task();
		if (!s_gpio_isr_service_installed) {
			esp_err_t ret = gpio_install_isr_service(0);
			if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
				s_gpio_isr_service_installed = true;
			} else {
				resp_payload->resp = ret;
				return ESP_OK;
			}
		}
	}

	for (int pin = 0; pin < GPIO_NUM_MAX; ++pin) {
		if (config.pin_bit_mask & (1ULL << pin)) {
			if (isr_requested) {
				gpio_set_intr_type(pin, config.intr_type);
				gpio_isr_handler_remove(pin);
				gpio_isr_handler_add(pin, gpio_expander_isr_handler,
						(void *)(uintptr_t)pin);
				gpio_intr_enable(pin);
			} else if (s_gpio_isr_service_installed) {
				gpio_intr_disable(pin);
				gpio_isr_handler_remove(pin);
			}
		}
	}

	return ESP_OK;
}

esp_err_t req_gpio_reset(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioResetPin, resp_gpio_reset,
			RpcReqGpioResetPin, req_gpio_reset_pin,
			rpc__resp__gpio_reset_pin__init);

	gpio_num_t gpio_num;
	gpio_num = req_payload->gpio_num;

	if (!transport_gpio_pin_guard_is_eligible(gpio_num)) {
		ESP_LOGE(TAG, "GPIO pin %d is not allowed to be configured", gpio_num);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	RPC_RET_FAIL_IF(gpio_reset_pin(gpio_num));

	return ESP_OK;
}

esp_err_t req_gpio_set_level(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioSetLevel, resp_gpio_set_level,
			RpcReqGpioSetLevel, req_gpio_set_level,
			rpc__resp__gpio_set_level__init);

	gpio_num_t gpio_num;
	gpio_num = req_payload->gpio_num;

	uint32_t level;
	level = req_payload->level;

	if (!transport_gpio_pin_guard_is_eligible(gpio_num)) {
		ESP_LOGE(TAG, "GPIO pin %d is not allowed to be configured", gpio_num);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	RPC_RET_FAIL_IF(gpio_set_level(gpio_num, level));

	return ESP_OK;
}

esp_err_t req_gpio_get_level(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioGetLevel, resp_gpio_get_level,
			RpcReqGpioGetLevel, req_gpio_get_level,
			rpc__resp__gpio_get_level__init);

	gpio_num_t gpio_num;
	gpio_num = req_payload->gpio_num;

	if (!transport_gpio_pin_guard_is_eligible(gpio_num)) {
		ESP_LOGE(TAG, "GPIO pin %d is not allowed to be configured", gpio_num);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	int level = gpio_get_level(gpio_num);

	resp_payload->level = level;

	return ESP_OK;
}

esp_err_t req_gpio_set_direction(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioSetDirection, resp_gpio_set_direction,
			RpcReqGpioSetDirection, req_gpio_set_direction,
			rpc__resp__gpio_set_direction__init);

	gpio_num_t gpio_num;
	gpio_num = req_payload->gpio_num;

	gpio_mode_t mode;
	mode = req_payload->mode;

	if (!transport_gpio_pin_guard_is_eligible(gpio_num)) {
		ESP_LOGE(TAG, "GPIO pin %d is not allowed to be configured", gpio_num);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	RPC_RET_FAIL_IF(gpio_set_direction(gpio_num, mode));

	return ESP_OK;
}

esp_err_t req_gpio_input_enable(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioInputEnable, resp_gpio_input_enable,
			RpcReqGpioInputEnable, req_gpio_input_enable,
			rpc__resp__gpio_input_enable__init);

	gpio_num_t gpio_num;
	gpio_num = req_payload->gpio_num;

	if (!transport_gpio_pin_guard_is_eligible(gpio_num)) {
		ESP_LOGE(TAG, "GPIO pin %d is not allowed to be configured", gpio_num);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	RPC_RET_FAIL_IF(gpio_input_enable(gpio_num));

	return ESP_OK;
}

esp_err_t req_gpio_set_pull_mode(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioSetPullMode, resp_gpio_set_pull_mode,
			RpcReqGpioSetPullMode, req_gpio_set_pull_mode,
			rpc__resp__gpio_set_pull_mode__init);

	gpio_num_t gpio_num;
	gpio_num = req_payload->gpio_num;

	gpio_pull_mode_t pull_mode;
	pull_mode = req_payload->pull;

	if (!transport_gpio_pin_guard_is_eligible(gpio_num)) {
		ESP_LOGE(TAG, "GPIO pin %d is not allowed to be configured", gpio_num);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	RPC_RET_FAIL_IF(gpio_set_pull_mode(gpio_num, pull_mode));

	return ESP_OK;
}

esp_err_t req_gpio_intr_control(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGpioIntrControl, resp_gpio_intr_control,
			RpcReqGpioIntrControl, req_gpio_intr_control,
			rpc__resp__gpio_intr_control__init);

	gpio_num_t gpio_num = req_payload->gpio_num;
	int cmd = req_payload->cmd;

	switch (cmd) {
	case RPC__REQ__GPIO_INTR__MSG_ID__GPIO_INTR_SET_TYPE: {
		gpio_int_type_t intr_type = req_payload->intr_type;
		if (!gpio_expander_isr_allowed(gpio_num, intr_type == GPIO_INTR_DISABLE)) {
			ESP_LOGE(TAG, "GPIO pin %d is not allowed for ISR", gpio_num);
			resp_payload->resp = ESP_ERR_NOT_SUPPORTED;
			return ESP_OK;
		}
		if (intr_type != GPIO_INTR_DISABLE) {
			gpio_expander_ensure_isr_task();
			if (!s_gpio_isr_service_installed) {
				esp_err_t ret = gpio_install_isr_service(0);
				if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
					s_gpio_isr_service_installed = true;
				} else {
					resp_payload->resp = ret;
					return ESP_OK;
				}
			}
			gpio_isr_handler_remove(gpio_num);
			gpio_isr_handler_add(gpio_num, gpio_expander_isr_handler,
					(void *)(uintptr_t)gpio_num);
		}
		RPC_RET_FAIL_IF(gpio_set_intr_type(gpio_num, intr_type));
		break;
	}
	case RPC__REQ__GPIO_INTR__MSG_ID__GPIO_INTR_ENABLE:
		if (!gpio_expander_isr_allowed(gpio_num, false)) {
			ESP_LOGE(TAG, "GPIO pin %d is not allowed for ISR", gpio_num);
			resp_payload->resp = ESP_ERR_NOT_SUPPORTED;
			return ESP_OK;
		}
		RPC_RET_FAIL_IF(gpio_intr_enable(gpio_num));
		break;
	case RPC__REQ__GPIO_INTR__MSG_ID__GPIO_INTR_DISABLE:
		if (!gpio_expander_isr_allowed(gpio_num, true)) {
			ESP_LOGE(TAG, "GPIO pin %d is not allowed for ISR", gpio_num);
			resp_payload->resp = ESP_ERR_NOT_SUPPORTED;
			return ESP_OK;
		}
		RPC_RET_FAIL_IF(gpio_intr_disable(gpio_num));
		break;
	default:
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		break;
	}

	return ESP_OK;
}
#endif
