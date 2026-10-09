#if EH_HOST_FEAT_RF_CERT_READY
case RPC_ID__Req_PhyWifiTx:
case RPC_ID__Req_PhyWifiRx:
case RPC_ID__Req_PhyWifiTxTone:
case RPC_ID__Req_PhyGetRxResult:
case RPC_ID__Req_PhyCmdStop:
case RPC_ID__Req_PhyTxContinEn:
case RPC_ID__Req_PhyCbw40mEn:
case RPC_ID__Req_PhyWifi11axTxSet:
case RPC_ID__Req_PhyBleTx:
case RPC_ID__Req_PhyBleRx:
case RPC_ID__Req_PhyBtTxTone:
    return rpc_ext_v2_pick_req_rf_cert(msg_id);
#endif
