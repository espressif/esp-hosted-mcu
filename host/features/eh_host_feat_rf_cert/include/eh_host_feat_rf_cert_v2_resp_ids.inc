#if EH_HOST_FEAT_RF_CERT_READY
case RPC_ID__Resp_PhyWifiTx:
case RPC_ID__Resp_PhyWifiRx:
case RPC_ID__Resp_PhyWifiTxTone:
case RPC_ID__Resp_PhyGetRxResult:
case RPC_ID__Resp_PhyCmdStop:
case RPC_ID__Resp_PhyTxContinEn:
case RPC_ID__Resp_PhyCbw40mEn:
case RPC_ID__Resp_PhyWifi11axTxSet:
case RPC_ID__Resp_PhyBleTx:
case RPC_ID__Resp_PhyBleRx:
case RPC_ID__Resp_PhyBtTxTone:
    return rpc_ext_v2_parse_resp_rf_cert(rpc, c);
#endif
