/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

#include "eh_cp_master_config.h"
#if EH_CP_FEAT_RPC_EXT_V2_READY

#if EH_CP_FEAT_RF_CERT_READY

#include "eh_cp_feat_rpc_ext_v2_priv.h"
#include "eh_cp_feat_rf_cert.h"
#include "esp_phy_cert_test.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rpc_rf_cert";

/* esp_phy_wifi_tx/rx and esp_phy_ble_tx/rx do not return while a test runs, and
 * a packet count of 0 means never.  On the RPC dispatcher that would deadlock
 * it and the stop command could never arrive, so each gets its own task —
 * names and stack sizes as in ESP-IDF's cert_test.
 *
 * A task blocked in the PHY holds its stack until the test ends, so starting
 * one test after another without a stop would pile them up.  The rule is the
 * protocol itself: start, stop, start.  A second start without a stop is
 * refused; once a stop is issued the next start is allowed, whether or not
 * the PHY call has returned yet. */
#define CERT_TASK_PRIO 2

static bool s_test_armed;

static esp_err_t cert_task_start(TaskFunction_t fn, const char *name,
		uint32_t stack)
{
	if (s_test_armed) {
		/* No command name here: the host decides what its console calls this. */
		ESP_LOGE(TAG, "a test is already running; stop it first");
		return ESP_ERR_NOT_FINISHED;
	}
	if (xTaskCreate(fn, name, stack, NULL, CERT_TASK_PRIO, NULL) != pdPASS) {
		return ESP_ERR_NO_MEM;
	}
	s_test_armed = true;
	return ESP_OK;
}

static void cert_task_done(void)
{
	s_test_armed = false;
	vTaskDelete(NULL);
}

/* Every command needs cert mode entered first, or the PHY test library is
 * uninitialised and the call faults. */
#define RPC_RF_CERT_RET_IF_NOT_INITED()                                       \
	do {                                                                      \
		if (!eh_cp_feat_rf_cert_is_inited()) {                                \
			ESP_LOGE(TAG, "RF cert mode not entered");                        \
			resp_payload->resp = ESP_ERR_INVALID_STATE;                       \
			return ESP_OK;                                                    \
		}                                                                     \
	} while (0)

/* Radio-agnostic, so outside the Wi-Fi guard: the RX result carries a flag for
 * which radio it came from, and stop ends whichever test runs. */
esp_err_t req_phy_get_rx_result(Rpc *req, Rpc *resp, void *priv_data)
{
	esp_phy_rx_result_t result = {0};

	RPC_TEMPLATE_SIMPLE(RpcRespPhyGetRxResult, resp_phy_get_rx_result,
			RpcReqPhyGetRxResult, req_phy_get_rx_result,
			rpc__resp__phy_get_rx_result__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	esp_phy_get_rx_result(&result);

	resp_payload->rx_correct_count = result.phy_rx_correct_count;
	resp_payload->rx_rssi          = result.phy_rx_rssi;
	resp_payload->rx_total_count   = result.phy_rx_total_count;
	resp_payload->rx_result_flag   = result.phy_rx_result_flag;

	ESP_LOGI(TAG, "rx_result: correct=%u total=%u rssi=%d flag=%u",
			(unsigned)result.phy_rx_correct_count,
			(unsigned)result.phy_rx_total_count,
			(int)result.phy_rx_rssi,
			(unsigned)result.phy_rx_result_flag);
	return ESP_OK;
}

esp_err_t req_phy_cmd_stop(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyCmdStop, resp_phy_cmd_stop,
			RpcReqPhyCmdStop, req_phy_cmd_stop,
			rpc__resp__phy_cmd_stop__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	ESP_LOGI(TAG, "cmd_stop: value=%u", (unsigned)req_payload->value);
	esp_phy_test_start_stop((uint8_t)req_payload->value);
	s_test_armed = false;
	return ESP_OK;
}

#if EH_CP_FEAT_RF_CERT_WIFI

static struct {
	uint32_t chan;
	esp_phy_wifi_rate_t rate;
	int8_t backoff;
	uint32_t length_byte;
	uint32_t packet_delay;
	uint32_t packet_num;
} s_wifi_tx;

static struct {
	uint32_t chan;
	esp_phy_wifi_rate_t rate;
} s_wifi_rx;

static void cert_wifi_tx(void *arg)
{
	eh_cp_feat_rf_cert_test_begin();
	esp_phy_wifi_tx(s_wifi_tx.chan, s_wifi_tx.rate, s_wifi_tx.backoff,
			s_wifi_tx.length_byte, s_wifi_tx.packet_delay,
			s_wifi_tx.packet_num);
	cert_task_done();
}

static void cert_wifi_rx(void *arg)
{
	eh_cp_feat_rf_cert_test_begin();
	esp_phy_wifi_rx(s_wifi_rx.chan, s_wifi_rx.rate);
	cert_task_done();
}

esp_err_t req_phy_wifi_tx(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyWifiTx, resp_phy_wifi_tx,
			RpcReqPhyWifiTx, req_phy_wifi_tx,
			rpc__resp__phy_wifi_tx__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	if (req_payload->rate >= PHY_WIFI_RATE_MAX) {
		ESP_LOGE(TAG, "rate 0x%x out of range", (unsigned)req_payload->rate);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	/* Channel is unchecked, as in cert_test.  ESP-IDF documents 1~14; what a
	 * dual-band part accepts beyond that is the PHY library's business, and
	 * a check here would only invent a limit ESP-IDF does not impose. */
	ESP_LOGI(TAG, "wifi_tx: chan=%u rate=0x%x backoff=%d len=%u delay=%u num=%u",
			(unsigned)req_payload->chan, (unsigned)req_payload->rate,
			(int)req_payload->backoff, (unsigned)req_payload->length_byte,
			(unsigned)req_payload->packet_delay,
			(unsigned)req_payload->packet_num);

	s_wifi_tx.chan         = req_payload->chan;
	s_wifi_tx.rate         = (esp_phy_wifi_rate_t)req_payload->rate;
	s_wifi_tx.backoff      = (int8_t)req_payload->backoff;
	s_wifi_tx.length_byte  = req_payload->length_byte;
	s_wifi_tx.packet_delay = req_payload->packet_delay;
	s_wifi_tx.packet_num   = req_payload->packet_num;
	resp_payload->resp = cert_task_start(cert_wifi_tx, "cert_wifi_tx", 1024 * 10);
	return ESP_OK;
}

esp_err_t req_phy_wifi_rx(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyWifiRx, resp_phy_wifi_rx,
			RpcReqPhyWifiRx, req_phy_wifi_rx,
			rpc__resp__phy_wifi_rx__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	if (req_payload->rate >= PHY_WIFI_RATE_MAX) {
		ESP_LOGE(TAG, "rate 0x%x out of range", (unsigned)req_payload->rate);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	ESP_LOGI(TAG, "wifi_rx: chan=%u rate=0x%x",
			(unsigned)req_payload->chan, (unsigned)req_payload->rate);

	s_wifi_rx.chan = req_payload->chan;
	s_wifi_rx.rate = (esp_phy_wifi_rate_t)req_payload->rate;
	resp_payload->resp = cert_task_start(cert_wifi_rx, "cert_wifi_rx", 1024 * 20);
	return ESP_OK;
}

esp_err_t req_phy_wifi_tx_tone(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyWifiTxTone, resp_phy_wifi_tx_tone,
			RpcReqPhyWifiTxTone, req_phy_wifi_tx_tone,
			rpc__resp__phy_wifi_tx_tone__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();


	ESP_LOGI(TAG, "wifi_tx_tone: start=%u chan=%u backoff=%u",
			(unsigned)req_payload->start, (unsigned)req_payload->chan,
			(unsigned)req_payload->backoff);

	esp_phy_wifi_tx_tone(req_payload->start, req_payload->chan,
			req_payload->backoff);
	return ESP_OK;
}



esp_err_t req_phy_tx_contin_en(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyTxContinEn, resp_phy_tx_contin_en,
			RpcReqPhyTxContinEn, req_phy_tx_contin_en,
			rpc__resp__phy_tx_contin_en__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	ESP_LOGI(TAG, "tx_contin_en: %d", (int)req_payload->contin_en);
	esp_phy_tx_contin_en(req_payload->contin_en);
	return ESP_OK;
}

esp_err_t req_phy_cbw40m_en(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyCbw40mEn, resp_phy_cbw40m_en,
			RpcReqPhyCbw40mEn, req_phy_cbw40m_en,
			rpc__resp__phy_cbw40m_en__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	ESP_LOGI(TAG, "cbw40m_en: %d", (int)req_payload->en);
	esp_phy_cbw40m_en(req_payload->en);
	return ESP_OK;
}

#if EH_CP_FEAT_RF_CERT_HE
esp_err_t req_phy_wifi_11ax_tx_set(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyWifi11axTxSet, resp_phy_wifi_11ax_tx_set,
			RpcReqPhyWifi11axTxSet, req_phy_wifi_11ax_tx_set,
			rpc__resp__phy_wifi11ax_tx_set__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	ESP_LOGI(TAG, "11ax_tx_set: he_format=%u pe=%u giltf=%u ru=%u",
			(unsigned)req_payload->he_format, (unsigned)req_payload->pe,
			(unsigned)req_payload->giltf_num, (unsigned)req_payload->ru_index);

	esp_phy_11ax_tx_set(req_payload->he_format, req_payload->pe,
			req_payload->giltf_num, req_payload->ru_index);
	return ESP_OK;
}
#endif /* EH_CP_FEAT_RF_CERT_HE */

#endif /* EH_CP_FEAT_RF_CERT_WIFI */

#if EH_CP_FEAT_RF_CERT_BLE

static struct {
	uint32_t txpwr;
	uint32_t chan;
	uint32_t len;
	esp_phy_ble_type_t data_type;
	uint32_t syncw;
	esp_phy_ble_rate_t rate;
	uint32_t tx_num_in;
} s_ble_tx;

static struct {
	uint32_t chan;
	uint32_t syncw;
	esp_phy_ble_rate_t rate;
} s_ble_rx;

static void cert_ble_tx(void *arg)
{
	eh_cp_feat_rf_cert_test_begin();
	esp_phy_ble_tx(s_ble_tx.txpwr, s_ble_tx.chan, s_ble_tx.len,
			s_ble_tx.data_type, s_ble_tx.syncw, s_ble_tx.rate,
			s_ble_tx.tx_num_in);
	cert_task_done();
}

static void cert_ble_rx(void *arg)
{
	eh_cp_feat_rf_cert_test_begin();
	esp_phy_ble_rx(s_ble_rx.chan, s_ble_rx.syncw, s_ble_rx.rate);
	cert_task_done();
}

esp_err_t req_phy_ble_tx(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyBleTx, resp_phy_ble_tx,
			RpcReqPhyBleTx, req_phy_ble_tx,
			rpc__resp__phy_ble_tx__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	if (req_payload->chan > 39) {
		ESP_LOGE(TAG, "BLE channel %u out of range 0..39",
				(unsigned)req_payload->chan);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}
	if (req_payload->len > 255) {
		ESP_LOGE(TAG, "BLE payload len %u out of range 0..255",
				(unsigned)req_payload->len);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}
	if (req_payload->rate >= PHY_BLE_RATE_MAX ||
			req_payload->data_type >= PHY_BLE_TYPE_MAX) {
		ESP_LOGE(TAG, "BLE rate/type out of range");
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	ESP_LOGI(TAG, "ble_tx: pwr=%u chan=%u len=%u type=%u syncw=0x%08x rate=%u num=%u",
			(unsigned)req_payload->txpwr, (unsigned)req_payload->chan,
			(unsigned)req_payload->len, (unsigned)req_payload->data_type,
			(unsigned)req_payload->syncw, (unsigned)req_payload->rate,
			(unsigned)req_payload->tx_num);

	s_ble_tx.txpwr     = req_payload->txpwr;
	s_ble_tx.chan      = req_payload->chan;
	s_ble_tx.len       = req_payload->len;
	s_ble_tx.data_type = (esp_phy_ble_type_t)req_payload->data_type;
	s_ble_tx.syncw     = req_payload->syncw;
	s_ble_tx.rate      = (esp_phy_ble_rate_t)req_payload->rate;
	s_ble_tx.tx_num_in = req_payload->tx_num;
	resp_payload->resp = cert_task_start(cert_ble_tx, "cert_ble_tx", 4096);
	return ESP_OK;
}

esp_err_t req_phy_ble_rx(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyBleRx, resp_phy_ble_rx,
			RpcReqPhyBleRx, req_phy_ble_rx,
			rpc__resp__phy_ble_rx__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	if (req_payload->chan > 39) {
		ESP_LOGE(TAG, "BLE channel %u out of range 0..39",
				(unsigned)req_payload->chan);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}
	if (req_payload->rate >= PHY_BLE_RATE_MAX) {
		ESP_LOGE(TAG, "BLE rate %u out of range", (unsigned)req_payload->rate);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	ESP_LOGI(TAG, "ble_rx: chan=%u syncw=0x%08x rate=%u",
			(unsigned)req_payload->chan, (unsigned)req_payload->syncw,
			(unsigned)req_payload->rate);

	s_ble_rx.chan  = req_payload->chan;
	s_ble_rx.syncw = req_payload->syncw;
	s_ble_rx.rate  = (esp_phy_ble_rate_t)req_payload->rate;
	resp_payload->resp = cert_task_start(cert_ble_rx, "cert_ble_rx", 4096);
	return ESP_OK;
}

esp_err_t req_phy_bt_tx_tone(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespPhyBtTxTone, resp_phy_bt_tx_tone,
			RpcReqPhyBtTxTone, req_phy_bt_tx_tone,
			rpc__resp__phy_bt_tx_tone__init);

	RPC_RF_CERT_RET_IF_NOT_INITED();

	if (req_payload->chan > 39) {
		ESP_LOGE(TAG, "BLE channel %u out of range 0..39",
				(unsigned)req_payload->chan);
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		return ESP_OK;
	}

	ESP_LOGI(TAG, "bt_tx_tone: start=%u chan=%u power=%u",
			(unsigned)req_payload->start, (unsigned)req_payload->chan,
			(unsigned)req_payload->power);

	esp_phy_bt_tx_tone(req_payload->start, req_payload->chan,
			req_payload->power);
	return ESP_OK;
}

#endif /* EH_CP_FEAT_RF_CERT_BLE */

esp_err_t req_feature_control_rf_cert(RpcReqFeatureControl *req_payload,
                                      RpcRespFeatureControl *resp_payload)
{
	switch (req_payload->command) {
	case RPC_FEATURE_COMMAND__Feature_Command_Init:
		RPC_RET_FAIL_IF(eh_cp_feat_rf_cert_init());
		break;
	case RPC_FEATURE_COMMAND__Feature_Command_Deinit:
		RPC_RET_FAIL_IF(eh_cp_feat_rf_cert_deinit());
		break;
	case RPC_FEATURE_COMMAND__Feature_Command_Query:
		switch (req_payload->option) {
		case RPC_FEATURE_OPTION__Feature_Option_Query_Configured:
			resp_payload->resp = ESP_OK;
			break;
		case RPC_FEATURE_OPTION__Feature_Option_Query_Inited:
		case RPC_FEATURE_OPTION__Feature_Option_Query_Ready:
			resp_payload->resp = eh_cp_feat_rf_cert_is_inited() ?
					ESP_OK : ESP_ERR_INVALID_STATE;
			break;
		default:
			/* Enable/Disable have no meaning: cert mode cannot be left
			 * without a restart, so there is no enabled/disabled state. */
			ESP_LOGE(TAG, "error: invalid RF cert Query Option");
			resp_payload->resp = ESP_ERR_INVALID_ARG;
			break;
		}
		break;
	default:
		ESP_LOGE(TAG, "error: invalid RF cert Feature Control");
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		break;
	}
	return ESP_OK;
}

#endif /* EH_CP_FEAT_RF_CERT_READY */
#endif /* EH_CP_FEAT_RPC_EXT_V2_READY */
