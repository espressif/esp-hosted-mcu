#if EH_HOST_FEAT_BT_TX_POWER_READY
case RPC_ID__Req_BtSetTxPower:
case RPC_ID__Req_BtGetTxPower:
    return rpc_ext_v2_pick_req_bt(msg_id);
#endif
