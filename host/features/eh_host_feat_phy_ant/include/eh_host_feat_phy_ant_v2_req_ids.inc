#if EH_HOST_FEAT_PHY_ANT_READY
case RPC_ID__Req_WifiSetAntGpio:
case RPC_ID__Req_WifiGetAntGpio:
case RPC_ID__Req_WifiSetAnt:
case RPC_ID__Req_WifiGetAnt:
    return rpc_ext_v2_pick_req_phy_ant(msg_id);
#endif
