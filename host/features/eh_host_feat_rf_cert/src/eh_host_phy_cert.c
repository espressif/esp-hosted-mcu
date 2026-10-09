/* SPDX-License-Identifier: Apache-2.0 */
/* RF certification test — native host API over RPC. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"

#include "eh_host_feat_rpc_ext_v2.h"
#include "eh_host_feat_rpc_ext_v2_types.h"
#include "eh_host_feat_rpc.h"
#include "gen_v2.h"

#include "eh_host_phy_cert.h"

#if EH_HOST_FEAT_RF_CERT_READY

/* Send a request whose response carries only a status. */
static esp_err_t rf_cert_send(int32_t msg_id, eh_rpc_ctrl_cmd_t *req)
{
    eh_rpc_ctrl_cmd_t *r = NULL;

    if (eh_host_feat_rpc_request_sync(msg_id, req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    int status = r->resp_event_status;
    eh_rpc_ctrl_cmd_free(r);
    return (esp_err_t)status;
}

static esp_err_t rf_cert_feature_cmd(RpcFeatureCommand command,
                                     RpcFeatureOption option)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.feat_ctrl.feature = RPC_FEATURE__Feature_Rf_Cert;
    req->u.feat_ctrl.command = command;
    req->u.feat_ctrl.option  = option;
    return rf_cert_send(RPC_ID__Req_FeatureControl, req);
}

esp_err_t eh_host_phy_cert_init(void)
{
    return rf_cert_feature_cmd(RPC_FEATURE_COMMAND__Feature_Command_Init,
                               RPC_FEATURE_OPTION__Feature_Option_None);
}

esp_err_t eh_host_phy_cert_deinit(void)
{
    return rf_cert_feature_cmd(RPC_FEATURE_COMMAND__Feature_Command_Deinit,
                               RPC_FEATURE_OPTION__Feature_Option_None);
}

esp_err_t eh_host_phy_cert_query(eh_host_phy_cert_query_t what)
{
    RpcFeatureOption option;

    switch (what) {
    case EH_HOST_PHY_CERT_QUERY_CONFIGURED:
        option = RPC_FEATURE_OPTION__Feature_Option_Query_Configured;
        break;
    case EH_HOST_PHY_CERT_QUERY_INITED:
        option = RPC_FEATURE_OPTION__Feature_Option_Query_Inited;
        break;
    case EH_HOST_PHY_CERT_QUERY_READY:
        option = RPC_FEATURE_OPTION__Feature_Option_Query_Ready;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return rf_cert_feature_cmd(RPC_FEATURE_COMMAND__Feature_Command_Query,
                               option);
}

esp_err_t eh_host_phy_cert_wifi_tx(uint32_t chan, esp_phy_wifi_rate_t rate,
                                 int8_t backoff, uint32_t length_byte,
                                 uint32_t packet_delay, uint32_t packet_num)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_wifi_tx.chan         = chan;
    req->u.phy_wifi_tx.rate         = rate;
    req->u.phy_wifi_tx.backoff      = backoff;
    req->u.phy_wifi_tx.length_byte  = length_byte;
    req->u.phy_wifi_tx.packet_delay = packet_delay;
    req->u.phy_wifi_tx.packet_num   = packet_num;
    return rf_cert_send(RPC_ID__Req_PhyWifiTx, req);
}

esp_err_t eh_host_phy_cert_wifi_rx(uint32_t chan, esp_phy_wifi_rate_t rate)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_wifi_rx.chan = chan;
    req->u.phy_wifi_rx.rate = rate;
    return rf_cert_send(RPC_ID__Req_PhyWifiRx, req);
}

esp_err_t eh_host_phy_cert_wifi_tx_tone(uint32_t start, uint32_t chan,
                                      uint32_t backoff)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_tone.start   = start;
    req->u.phy_tone.chan    = chan;
    req->u.phy_tone.backoff = backoff;
    return rf_cert_send(RPC_ID__Req_PhyWifiTxTone, req);
}

esp_err_t eh_host_phy_cert_get_rx_result(esp_phy_rx_result_t *rx_result)
{
    eh_rpc_ctrl_cmd_t *req = NULL;
    eh_rpc_ctrl_cmd_t *r = NULL;
    int status;

    if (!rx_result) {
        return ESP_ERR_INVALID_ARG;
    }
    req = eh_rpc_ctrl_cmd_alloc();
    if (!req) {
        return ESP_FAIL;
    }
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_PhyGetRxResult, req,
                                      (void **)&r) != 0) {
        return ESP_FAIL;
    }
    status = r->resp_event_status;
    if (status == 0) {
        rx_result->phy_rx_correct_count = r->u.phy_rx_result.rx_correct_count;
        rx_result->phy_rx_rssi          = r->u.phy_rx_result.rx_rssi;
        rx_result->phy_rx_total_count   = r->u.phy_rx_result.rx_total_count;
        rx_result->phy_rx_result_flag   = r->u.phy_rx_result.rx_result_flag;
    }
    eh_rpc_ctrl_cmd_free(r);
    return (esp_err_t)status;
}

esp_err_t eh_host_phy_cert_test_start_stop(uint8_t value)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_cmd_stop.value = value;
    return rf_cert_send(RPC_ID__Req_PhyCmdStop, req);
}

esp_err_t eh_host_phy_cert_tx_contin_en(bool contin_en)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_flag.en = contin_en;
    return rf_cert_send(RPC_ID__Req_PhyTxContinEn, req);
}

esp_err_t eh_host_phy_cert_cbw40m_en(bool en)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_flag.en = en;
    return rf_cert_send(RPC_ID__Req_PhyCbw40mEn, req);
}

esp_err_t eh_host_phy_cert_11ax_tx_set(uint32_t he_format, uint32_t pe,
                                     uint32_t giltf_num, uint32_t ru_index)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_11ax.he_format = he_format;
    req->u.phy_11ax.pe        = pe;
    req->u.phy_11ax.giltf_num = giltf_num;
    req->u.phy_11ax.ru_index  = ru_index;
    return rf_cert_send(RPC_ID__Req_PhyWifi11axTxSet, req);
}

esp_err_t eh_host_phy_cert_ble_tx(uint32_t txpwr, uint32_t chan, uint32_t len,
                                esp_phy_ble_type_t data_type, uint32_t syncw,
                                esp_phy_ble_rate_t rate, uint32_t tx_num_in)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_ble_tx.txpwr     = txpwr;
    req->u.phy_ble_tx.chan      = chan;
    req->u.phy_ble_tx.len       = len;
    req->u.phy_ble_tx.data_type = data_type;
    req->u.phy_ble_tx.syncw     = syncw;
    req->u.phy_ble_tx.rate      = rate;
    req->u.phy_ble_tx.tx_num    = tx_num_in;
    return rf_cert_send(RPC_ID__Req_PhyBleTx, req);
}

esp_err_t eh_host_phy_cert_ble_rx(uint32_t chan, uint32_t syncw,
                                esp_phy_ble_rate_t rate)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_ble_rx.chan  = chan;
    req->u.phy_ble_rx.syncw = syncw;
    req->u.phy_ble_rx.rate  = rate;
    return rf_cert_send(RPC_ID__Req_PhyBleRx, req);
}

esp_err_t eh_host_phy_cert_bt_tx_tone(uint32_t start, uint32_t chan,
                                    uint32_t power)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) {
        return ESP_FAIL;
    }
    req->u.phy_tone.start   = start;
    req->u.phy_tone.chan    = chan;
    req->u.phy_tone.backoff = power;
    return rf_cert_send(RPC_ID__Req_PhyBtTxTone, req);
}

#endif /* EH_HOST_FEAT_RF_CERT_READY */
