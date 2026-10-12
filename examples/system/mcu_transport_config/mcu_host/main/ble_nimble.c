/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Stage 3: BLE advertising over the hosted HCI link. The controller runs on the
 * coprocessor (CONFIG_BT_CONTROLLER_DISABLED=y); esp_hosted_bt_host_stack_setup()
 * brings it up and binds NimBLE to the HCI byte-pipe. The hosted link is already
 * up (stage 1 did init + connect_to_slave), so we go straight to BT setup ->
 * nimble_port_init -> advertise on the sync callback.
 */
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "esp_hosted_bt_host_stack.h"
#include "example_private.h"

#define EX_BLE_NAME "eh-manual"

static const char *TAG = EH_EX_TAG;
static uint8_t s_addr_type;
static SemaphoreHandle_t s_adv_started;   /* given once advertising is live */

static int gap_event(struct ble_gap_event *ev, void *arg);

static void advertise(void)
{
    struct ble_gap_adv_params adv = {0};
    struct ble_hs_adv_fields fields = {0};
    const char *name = ble_svc_gap_device_name();

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    adv.conn_mode = BLE_GAP_CONN_MODE_UND;    /* connectable-undirected */
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;    /* general-discoverable */
    int rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    ESP_LOGI(TAG, "EH stage=ble advertising rc=%d name=%s", rc, name);
    if (rc == 0 && s_adv_started) {
        xSemaphoreGive(s_adv_started);        /* unblock example_ble_start() */
    }
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    if (ev->type == BLE_GAP_EVENT_DISCONNECT || ev->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        advertise();                          /* keep advertising after a peer leaves */
    }
    return 0;
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_addr_type);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "nimble stack reset; reason=%d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();                        /* returns only after nimble_port_stop() */
    nimble_port_freertos_deinit();
}

esp_err_t example_ble_start(void)
{
    s_adv_started = xSemaphoreCreateBinary();
    esp_hosted_bt_host_stack_cfg_t bt = ESP_HOSTED_BT_HOST_STACK_CONFIG_DEFAULT();
    esp_err_t err = esp_hosted_bt_host_stack_setup(&bt);   /* CP controller up + HCI bound */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_bt_host_stack_setup rc=%d", err);
        return err;
    }
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init rc=%d", err);
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_device_name_set(EX_BLE_NAME);
    nimble_port_freertos_init(host_task);
    /* Block until the sync callback has actually started advertising, so the
     * caller's "bring-up complete" truly means every stage is up (and the log
     * order is deterministic for the test). */
    if (s_adv_started && xSemaphoreTake(s_adv_started, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGW(TAG, "advertising not confirmed within 10s");
    }
    return ESP_OK;
}

void example_ble_stop(void)
{
    ble_gap_adv_stop();
    nimble_port_stop();
    nimble_port_deinit();
    esp_hosted_bt_host_stack_teardown();
}
