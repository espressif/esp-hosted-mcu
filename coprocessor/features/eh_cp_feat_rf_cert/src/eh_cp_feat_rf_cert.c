/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

#include "eh_cp_master_config.h"
#include "eh_cp_feat_rf_cert.h"
#include "eh_cp_core.h"
#include "esp_log.h"

#if EH_CP_FEAT_RF_CERT_READY
#include "esp_phy_cert_test.h"
#if EH_CP_FEAT_RF_CERT_WIFI
#include "esp_wifi.h"
#endif
#endif

static const char *TAG = "feat_rf_cert";

#if EH_CP_FEAT_RF_CERT_READY
static bool s_inited;

/* esp_phy_test_start_stop(3) arms the PHY before a TX/RX command; (0) ends it.
 * The IDF cert_test example sets 3 inside every TX/RX command handler, so this
 * coprocessor does the same and the host only ever sends a stop. */
#define EH_CP_RF_CERT_TEST_ARM   3

esp_err_t eh_cp_feat_rf_cert_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

#if EH_CP_FEAT_RF_CERT_WIFI
    {
        wifi_mode_t mode;

        /* Cert mode and the Wi-Fi driver cannot share the radio. */
        if (esp_wifi_get_mode(&mode) != ESP_ERR_WIFI_NOT_INIT) {
            ESP_LOGE(TAG, "Wi-Fi is initialised; cannot enter RF cert mode");
            return ESP_ERR_INVALID_STATE;
        }
    }

    /* Only exists on parts with Wi-Fi; the PHY test libs need it powered. */
    esp_wifi_power_domain_on();
#endif

    esp_phy_rftest_config(1);
    esp_phy_rftest_init();

    s_inited = true;
    ESP_LOGW(TAG, "RF cert mode entered - radio is now in test mode");
    ESP_LOGW(TAG, "restart the coprocessor to go back to normal operation");
    return ESP_OK;
}

esp_err_t eh_cp_feat_rf_cert_deinit(void)
{
    if (!s_inited) {
        return ESP_OK;
    }

    esp_phy_test_start_stop(0);
#if EH_CP_FEAT_RF_CERT_WIFI
    esp_wifi_power_domain_off();
#endif

    /* s_inited stays true: the PHY keeps its cert-mode state and only a
     * restart clears it.  Reporting "not inited" here would tell the host it
     * can use Wi-Fi again, which is false. */
    ESP_LOGW(TAG, "RF cert test stopped; PHY stays in cert mode until restart");
    return ESP_OK;
}

bool eh_cp_feat_rf_cert_is_inited(void)
{
    return s_inited;
}

void eh_cp_feat_rf_cert_test_begin(void)
{
    esp_phy_test_start_stop(EH_CP_RF_CERT_TEST_ARM);
}

#else /* !EH_CP_FEAT_RF_CERT_READY */

esp_err_t eh_cp_feat_rf_cert_init(void)
{
    ESP_LOGW(TAG, "RF cert feature not ready (needs ESP_PHY_ENABLE_CERT_TEST)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t eh_cp_feat_rf_cert_deinit(void)
{
    return ESP_OK;
}

bool eh_cp_feat_rf_cert_is_inited(void)
{
    return false;
}

void eh_cp_feat_rf_cert_test_begin(void)
{
}

#endif /* EH_CP_FEAT_RF_CERT_READY */

#if EH_CP_FEAT_RF_CERT_AUTO_INIT
EH_CP_FEAT_REGISTER(eh_cp_feat_rf_cert_init,
                   eh_cp_feat_rf_cert_deinit,
                   "feat_rf_cert", tskNO_AFFINITY, 240);
#endif
