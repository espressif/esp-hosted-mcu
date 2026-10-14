/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* POC super CP: superset of the per-example CP apps. Features self-register via
 * EH_CP_FEAT_REGISTER; app code only adds the peer-data echo the host examples expect. */

#include <inttypes.h>
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eh_cp_feat_peer_data.h"

static const char *TAG = "eh_cp_full";

/* msg ids match examples/peer_data_transfer host: request -> echo reply */
#define MSG_ID_CAT      1
#define MSG_ID_MEOW     2
#define MSG_ID_DOG      3
#define MSG_ID_WOOF     4
#define MSG_ID_HUMAN    5
#define MSG_ID_HELLO    6

static void on_peer_data(uint32_t msg_id, const uint8_t *data, size_t len, void *ctx)
{
    uint32_t reply = (uint32_t)(uintptr_t)ctx;
    ESP_LOGI(TAG, "peer data msg %" PRIu32 " len=%u -> echo %" PRIu32,
             msg_id, (unsigned int)len, reply);
    eh_cp_feat_peer_data_send(reply, data, len);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-Hosted super CP starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* peer_data auto-inits asynchronously (auto_feat_init_task); retry until it is up */
    static const uint32_t req[] = { MSG_ID_CAT, MSG_ID_DOG, MSG_ID_HUMAN };
    static const uint32_t rsp[] = { MSG_ID_MEOW, MSG_ID_WOOF, MSG_ID_HELLO };
    for (int i = 0; i < 3; i++) {
        int tries = 0;
        while ((ret = eh_cp_feat_peer_data_register_callback(req[i], on_peer_data,
                        (void *)(uintptr_t)rsp[i])) == ESP_ERR_INVALID_STATE && tries++ < 200) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        ESP_ERROR_CHECK(ret);
    }
    ESP_LOGI(TAG, "peer data echo ready");
}
