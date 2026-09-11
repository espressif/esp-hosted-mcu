/* SPDX-License-Identifier: Apache-2.0 */
#include "rpc_ext_v2_decode_priv.h"

#if EH_HOST_FEAT_RF_CERT_READY

int rpc_ext_v2_parse_resp_rf_cert(const Rpc *rpc, eh_rpc_ctrl_cmd_t *c)
{
    switch (rpc->msg_id) {
    case RPC_ID__Resp_PhyWifiTx:
        if (!rpc->resp_phy_wifi_tx) return -1;
        c->resp_event_status = rpc->resp_phy_wifi_tx->resp;
        return 0;
    case RPC_ID__Resp_PhyWifiRx:
        if (!rpc->resp_phy_wifi_rx) return -1;
        c->resp_event_status = rpc->resp_phy_wifi_rx->resp;
        return 0;
    case RPC_ID__Resp_PhyWifiTxTone:
        if (!rpc->resp_phy_wifi_tx_tone) return -1;
        c->resp_event_status = rpc->resp_phy_wifi_tx_tone->resp;
        return 0;
    case RPC_ID__Resp_PhyGetRxResult:
        if (!rpc->resp_phy_get_rx_result) return -1;
        c->resp_event_status = rpc->resp_phy_get_rx_result->resp;
        c->u.phy_rx_result.rx_correct_count =
                rpc->resp_phy_get_rx_result->rx_correct_count;
        c->u.phy_rx_result.rx_rssi =
                rpc->resp_phy_get_rx_result->rx_rssi;
        c->u.phy_rx_result.rx_total_count =
                rpc->resp_phy_get_rx_result->rx_total_count;
        c->u.phy_rx_result.rx_result_flag =
                rpc->resp_phy_get_rx_result->rx_result_flag;
        return 0;
    case RPC_ID__Resp_PhyCmdStop:
        if (!rpc->resp_phy_cmd_stop) return -1;
        c->resp_event_status = rpc->resp_phy_cmd_stop->resp;
        return 0;
    case RPC_ID__Resp_PhyTxContinEn:
        if (!rpc->resp_phy_tx_contin_en) return -1;
        c->resp_event_status = rpc->resp_phy_tx_contin_en->resp;
        return 0;
    case RPC_ID__Resp_PhyCbw40mEn:
        if (!rpc->resp_phy_cbw40m_en) return -1;
        c->resp_event_status = rpc->resp_phy_cbw40m_en->resp;
        return 0;
    case RPC_ID__Resp_PhyWifi11axTxSet:
        if (!rpc->resp_phy_wifi_11ax_tx_set) return -1;
        c->resp_event_status = rpc->resp_phy_wifi_11ax_tx_set->resp;
        return 0;
    case RPC_ID__Resp_PhyBleTx:
        if (!rpc->resp_phy_ble_tx) return -1;
        c->resp_event_status = rpc->resp_phy_ble_tx->resp;
        return 0;
    case RPC_ID__Resp_PhyBleRx:
        if (!rpc->resp_phy_ble_rx) return -1;
        c->resp_event_status = rpc->resp_phy_ble_rx->resp;
        return 0;
    case RPC_ID__Resp_PhyBtTxTone:
        if (!rpc->resp_phy_bt_tx_tone) return -1;
        c->resp_event_status = rpc->resp_phy_bt_tx_tone->resp;
        return 0;
    default:
        return 0;
    }
}

#endif /* EH_HOST_FEAT_RF_CERT_READY */
