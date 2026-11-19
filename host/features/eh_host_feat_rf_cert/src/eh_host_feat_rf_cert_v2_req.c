/* SPDX-License-Identifier: Apache-2.0 */
#include "rpc_ext_v2_pack_priv.h"

#if EH_HOST_FEAT_RF_CERT_READY

static int compose_req_phy_wifi_tx(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                   alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyWifiTx, req_phy_wifi_tx,
                  rpc__req__phy_wifi_tx__init);
    p->chan         = c->u.phy_wifi_tx.chan;
    p->rate         = c->u.phy_wifi_tx.rate;
    p->backoff      = c->u.phy_wifi_tx.backoff;
    p->length_byte  = c->u.phy_wifi_tx.length_byte;
    p->packet_delay = c->u.phy_wifi_tx.packet_delay;
    p->packet_num   = c->u.phy_wifi_tx.packet_num;
    return 0;
}

static int compose_req_phy_wifi_rx(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                   alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyWifiRx, req_phy_wifi_rx,
                  rpc__req__phy_wifi_rx__init);
    p->chan = c->u.phy_wifi_rx.chan;
    p->rate = c->u.phy_wifi_rx.rate;
    return 0;
}

static int compose_req_phy_wifi_tx_tone(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                        alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyWifiTxTone, req_phy_wifi_tx_tone,
                  rpc__req__phy_wifi_tx_tone__init);
    p->start   = c->u.phy_tone.start;
    p->chan    = c->u.phy_tone.chan;
    p->backoff = c->u.phy_tone.backoff;
    return 0;
}

static int compose_req_phy_get_rx_result(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                         alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyGetRxResult, req_phy_get_rx_result,
                  rpc__req__phy_get_rx_result__init);
    (void)c;
    return 0;
}

static int compose_req_phy_cmd_stop(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                    alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyCmdStop, req_phy_cmd_stop,
                  rpc__req__phy_cmd_stop__init);
    p->value = c->u.phy_cmd_stop.value;
    return 0;
}

static int compose_req_phy_tx_contin_en(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                        alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyTxContinEn, req_phy_tx_contin_en,
                  rpc__req__phy_tx_contin_en__init);
    p->contin_en = c->u.phy_flag.en;
    return 0;
}

static int compose_req_phy_cbw40m_en(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                     alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyCbw40mEn, req_phy_cbw40m_en,
                  rpc__req__phy_cbw40m_en__init);
    p->en = c->u.phy_flag.en;
    return 0;
}

static int compose_req_phy_wifi_11ax_tx_set(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                            alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyWifi11axTxSet, req_phy_wifi_11ax_tx_set,
                  rpc__req__phy_wifi11ax_tx_set__init);
    p->he_format = c->u.phy_11ax.he_format;
    p->pe        = c->u.phy_11ax.pe;
    p->giltf_num = c->u.phy_11ax.giltf_num;
    p->ru_index  = c->u.phy_11ax.ru_index;
    return 0;
}

static int compose_req_phy_ble_tx(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                  alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyBleTx, req_phy_ble_tx, rpc__req__phy_ble_tx__init);
    p->txpwr     = c->u.phy_ble_tx.txpwr;
    p->chan      = c->u.phy_ble_tx.chan;
    p->len       = c->u.phy_ble_tx.len;
    p->data_type = c->u.phy_ble_tx.data_type;
    p->syncw     = c->u.phy_ble_tx.syncw;
    p->rate      = c->u.phy_ble_tx.rate;
    p->tx_num    = c->u.phy_ble_tx.tx_num;
    return 0;
}

static int compose_req_phy_ble_rx(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                  alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyBleRx, req_phy_ble_rx, rpc__req__phy_ble_rx__init);
    p->chan  = c->u.phy_ble_rx.chan;
    p->syncw = c->u.phy_ble_rx.syncw;
    p->rate  = c->u.phy_ble_rx.rate;
    return 0;
}

static int compose_req_phy_bt_tx_tone(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                      alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqPhyBtTxTone, req_phy_bt_tx_tone,
                  rpc__req__phy_bt_tx_tone__init);
    p->start = c->u.phy_tone.start;
    p->chan  = c->u.phy_tone.chan;
    p->power = c->u.phy_tone.backoff;
    return 0;
}

compose_fn rpc_ext_v2_pick_req_rf_cert(int32_t msg_id)
{
    switch (msg_id) {
    case RPC_ID__Req_PhyWifiTx:      return compose_req_phy_wifi_tx;
    case RPC_ID__Req_PhyWifiRx:      return compose_req_phy_wifi_rx;
    case RPC_ID__Req_PhyWifiTxTone:  return compose_req_phy_wifi_tx_tone;
    case RPC_ID__Req_PhyGetRxResult: return compose_req_phy_get_rx_result;
    case RPC_ID__Req_PhyCmdStop:     return compose_req_phy_cmd_stop;
    case RPC_ID__Req_PhyTxContinEn:  return compose_req_phy_tx_contin_en;
    case RPC_ID__Req_PhyCbw40mEn:    return compose_req_phy_cbw40m_en;
    case RPC_ID__Req_PhyWifi11axTxSet: return compose_req_phy_wifi_11ax_tx_set;
    case RPC_ID__Req_PhyBleTx:       return compose_req_phy_ble_tx;
    case RPC_ID__Req_PhyBleRx:       return compose_req_phy_ble_rx;
    case RPC_ID__Req_PhyBtTxTone:    return compose_req_phy_bt_tx_tone;
    default:                         return NULL;
    }
}

#endif /* EH_HOST_FEAT_RF_CERT_READY */
