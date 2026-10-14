/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */
#include "eh_cp_master_config.h"
#if EH_CP_FEAT_WIFI_READY
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/wifi.h"
#include "eh_common_log.h"
#include "eh_cp.h"
#include "eh_cp_utils.h"
#include "eh_cp_feat_wifi.h"
#include "eh_cp_feat_wifi_event_publisher.h"
#endif
#include "eh_cp_core.h"          /* EH_CP_FEAT_REGISTER */
#include "eh_common_caps.h"
#include "eh_cp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "eh_check.h"

static const char *TAG = "ehcp_wifi";

/* Forward declarations for sub-extension init/deinit */
#if EH_CP_FEAT_WIFI_EXT_ENT_READY
extern esp_err_t eh_cp_feat_wifi_ext_ent_init(void);
extern esp_err_t eh_cp_feat_wifi_ext_ent_deinit(void);
#endif
#if EH_CP_FEAT_WIFI_EXT_ITWT_READY
extern esp_err_t eh_cp_feat_wifi_ext_itwt_init(void);
extern esp_err_t eh_cp_feat_wifi_ext_itwt_deinit(void);
#endif
#if EH_CP_FEAT_WIFI_EXT_DPP_READY
extern esp_err_t eh_cp_feat_wifi_ext_dpp_init(void);
extern esp_err_t eh_cp_feat_wifi_ext_dpp_deinit(void);
#endif

#if EH_CP_FEAT_WIFI_READY
#define WIFI_TX_RETRY_DELAY_MS           1

/* Hold the frame over transient Wi-Fi pool exhaustion so a burst paces on the
 * bus instead of dropping into a TCP RTO. Any other error is terminal. */
static esp_err_t wifi_tx_with_retry(wifi_interface_t wifi_if, void *payload,
                                    uint16_t payload_len)
{
    esp_err_t ret = ESP_OK;
    int retry = EH_CP_FEAT_WIFI_TX_RETRY_LIMIT;

    do {
        ret = esp_wifi_internal_tx(wifi_if, payload, payload_len);
        if (ret != ESP_ERR_NO_MEM)
            break;
        vTaskDelay(pdMS_TO_TICKS(WIFI_TX_RETRY_DELAY_MS));
    } while (--retry);

    return ret;
}

static eh_cp_traffic_cb_t s_bus_rx_sta_tx_filter;

void eh_cp_feat_wifi_set_bus_rx_sta_tx_filter(eh_cp_traffic_cb_t filter)
{
    s_bus_rx_sta_tx_filter = filter;
}

static esp_err_t bus_rx_sta_tx(void *buf, uint16_t len, void *eb)
{
    eh_cp_traffic_cb_t filter = s_bus_rx_sta_tx_filter;
    esp_err_t ret;

    if (filter && filter(buf, len, eb) != ESP_ERR_NOT_FOUND)
        return ESP_OK;

    ret = wifi_tx_with_retry(WIFI_IF_STA, buf, len);
#if ESP_PKT_STATS
    if (ret)
        pkt_stats.hs_bus_sta_fail++;
    else
        pkt_stats.hs_bus_sta_out++;
#endif
    return ret;
}

static esp_err_t bus_rx_ap_tx(void *buf, uint16_t len, void *eb)
{
    (void)eb;
    ESP_HEXLOGV("AP_Put", buf, len, 32);
    return wifi_tx_with_retry(WIFI_IF_AP, buf, len);
}
#endif

esp_err_t eh_cp_feat_wifi_init(void)
{
#if EH_CP_FEAT_WIFI_READY
    EH_CHECK_OK_WARN(eh_cp_wifi_event_publisher_init());
    EH_CHECK_OK_WARN(eh_cp_set_traffic_rx_cb(ESP_STA_IF, bus_rx_sta_tx));
    EH_CHECK_OK_WARN(eh_cp_set_traffic_rx_cb(ESP_AP_IF, bus_rx_ap_tx));
    /* ESP_WLAN_SUPPORT is an EXT cap (Tier-2, bit 0) — pass it in the ext slot,
     * not base. In the base slot it lands on bit 0 = ESP_WLAN_SDIO_SUPPORT, so
     * the host wrongly reports "WLAN over SDIO" on non-SDIO transports (UART).
     * The transport-specific base bit (SDIO/SPI) is set by populate_core_caps. */
    eh_cp_add_feat_cap_bits(0, ESP_WLAN_SUPPORT);
    eh_cp_add_feat_cap_bits_idx(EH_FEAT_IDX_WIFI, 0x1u /* WIFI_BASIC */);

    /* Init sub-extensions (order: enterprise → itwt → dpp) */
#if EH_CP_FEAT_WIFI_EXT_ENT_READY
    EH_CHECK_OK_WARN(eh_cp_feat_wifi_ext_ent_init());
#endif
#if EH_CP_FEAT_WIFI_EXT_ITWT_READY
    EH_CHECK_OK_WARN(eh_cp_feat_wifi_ext_itwt_init());
#endif
#if EH_CP_FEAT_WIFI_EXT_DPP_READY
    EH_CHECK_OK_WARN(eh_cp_feat_wifi_ext_dpp_init());
#endif

    ESP_LOGI(TAG, "WiFi feature init ok");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "WiFi feature init skipped (not ready)");
    return ESP_OK;
#endif
}

esp_err_t eh_cp_feat_wifi_deinit(void)
{
#if EH_CP_FEAT_WIFI_READY
    /* Deinit sub-extensions in reverse order */
#if EH_CP_FEAT_WIFI_EXT_DPP_READY
    EH_CHECK_OK_WARN(eh_cp_feat_wifi_ext_dpp_deinit());
#endif
#if EH_CP_FEAT_WIFI_EXT_ITWT_READY
    EH_CHECK_OK_WARN(eh_cp_feat_wifi_ext_itwt_deinit());
#endif
#if EH_CP_FEAT_WIFI_EXT_ENT_READY
    EH_CHECK_OK_WARN(eh_cp_feat_wifi_ext_ent_deinit());
#endif

    EH_CHECK_OK_WARN(eh_cp_set_traffic_rx_cb(ESP_AP_IF, NULL));
    EH_CHECK_OK_WARN(eh_cp_set_traffic_rx_cb(ESP_STA_IF, NULL));
    EH_CHECK_OK_WARN(eh_cp_wifi_event_publisher_deinit());
    eh_cp_clear_feat_cap_bits_idx(EH_FEAT_IDX_WIFI, 0x1u /* WIFI_BASIC */);
    eh_cp_clear_feat_cap_bits(0, ESP_WLAN_SUPPORT);
    ESP_LOGI(TAG, "WiFi feature deinit ok");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "WiFi feature deinit skipped (not ready)");
    return ESP_OK;
#endif
}

#if EH_CP_FEAT_WIFI_AUTO_INIT
EH_CP_FEAT_REGISTER(eh_cp_feat_wifi_init,
                   eh_cp_feat_wifi_deinit,
                   "feat_wifi", tskNO_AFFINITY, 200);
#endif
