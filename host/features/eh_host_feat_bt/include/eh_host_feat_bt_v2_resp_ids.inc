#if EH_HOST_FEAT_BT_TX_POWER_READY
case RPC_ID__Resp_BtSetTxPower:
case RPC_ID__Resp_BtGetTxPower:
    return rpc_ext_v2_parse_resp_bt(rpc, c);
#endif
