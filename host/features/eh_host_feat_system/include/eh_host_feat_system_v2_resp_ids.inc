#if EH_HOST_FEAT_SYSTEM_READY
case RPC_ID__Resp_GetMACAddress:
case RPC_ID__Resp_SetMacAddress:
case RPC_ID__Resp_IfaceMacAddrSetGet:
case RPC_ID__Resp_GetCoprocessorFwVersion:
case RPC_ID__Resp_AppGetDesc:
    return rpc_ext_v2_parse_resp_system(rpc, c);
#endif
