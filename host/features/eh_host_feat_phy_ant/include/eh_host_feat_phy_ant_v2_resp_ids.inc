#if EH_HOST_FEAT_PHY_ANT_READY
case RPC_ID__Resp_WifiSetAntGpio:
case RPC_ID__Resp_WifiGetAntGpio:
case RPC_ID__Resp_WifiSetAnt:
case RPC_ID__Resp_WifiGetAnt:
    return rpc_ext_v2_parse_resp_phy_ant(rpc, c);
#endif
