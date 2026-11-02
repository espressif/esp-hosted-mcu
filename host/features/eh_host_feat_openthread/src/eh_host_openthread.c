/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include "esp_err.h"

#include "eh_host_port.h"
#include "eh_host_port_openthread.h"

#include "eh_host_feat_rpc_ext_v2_types.h"
#include "eh_host_feat_rpc.h"
#include "gen_v2.h"

#include "eh_host_openthread.h"
#include "eh_host_openthread_priv.h"

#if EH_HOST_FEAT_OPENTHREAD_READY

static eh_host_port_mutex_t *s_rcp_lock;
static bool s_rcp_started;

static esp_err_t rcp_ctrl(RpcFeatureCommand command, RpcFeatureOption option)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;
    req->u.feat_ctrl.feature = RPC_FEATURE__Feature_Openthread_Rcp;
    req->u.feat_ctrl.command = command;
    req->u.feat_ctrl.option  = option;
    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_FeatureControl, req,
                                         (void **)&r) != 0) return ESP_FAIL;
    int rc = r->resp_event_status;
    eh_rpc_ctrl_cmd_free(r);
    return (esp_err_t)rc;
}

/* Made on first use: the app may start the RCP before feature init. */
static esp_err_t rcp_lock(void)
{
    if (!s_rcp_lock)
        s_rcp_lock = eh_host_port_mutex_create();
    if (!s_rcp_lock)
        return ESP_ERR_NO_MEM;
    eh_host_port_mutex_lock(s_rcp_lock);
    return ESP_OK;
}

esp_err_t eh_host_openthread_rcp_init(void)
{
    return rcp_ctrl(RPC_FEATURE_COMMAND__Feature_Command_Init,
                    RPC_FEATURE_OPTION__Feature_Option_None);
}

esp_err_t eh_host_openthread_rcp_deinit(void)
{
    return rcp_ctrl(RPC_FEATURE_COMMAND__Feature_Command_Deinit,
                    RPC_FEATURE_OPTION__Feature_Option_None);
}

/* The first spinel write calls it. */
esp_err_t eh_host_openthread_rcp_start(void)
{
    esp_err_t ret = rcp_lock();

    if (ret != ESP_OK)
        return ret;
    if (s_rcp_started)
        goto out;

    /* Also waits for the link. */
    ret = eh_host_openthread_rcp_query(EH_HOST_OPENTHREAD_QUERY_CONFIGURED);
    /* Channel first: the RCP sends at once. */
    if (ret == ESP_OK)
        ret = eh_host_openthread_transport_init();
    if (ret == ESP_OK)
        ret = rcp_ctrl(RPC_FEATURE_COMMAND__Feature_Command_Enable,
                       RPC_FEATURE_OPTION__Feature_Option_None);
    if (ret != ESP_OK)
        eh_host_openthread_transport_deinit();
    s_rcp_started = (ret == ESP_OK);
out:
    eh_host_port_mutex_unlock(s_rcp_lock);
    return ret;
}

esp_err_t eh_host_openthread_rcp_stop(void)
{
    esp_err_t ret = rcp_lock();

    if (ret != ESP_OK)
        return ret;
    ret = rcp_ctrl(RPC_FEATURE_COMMAND__Feature_Command_Disable,
                   RPC_FEATURE_OPTION__Feature_Option_None);
    if (ret == ESP_OK) {
        eh_host_openthread_transport_deinit();
        s_rcp_started = false;
    }
    eh_host_port_mutex_unlock(s_rcp_lock);
    return ret;
}

esp_err_t eh_host_openthread_rcp_query(eh_host_openthread_query_t query)
{
    RpcFeatureOption option;
    switch (query) {
    case EH_HOST_OPENTHREAD_QUERY_CONFIGURED:
        option = RPC_FEATURE_OPTION__Feature_Option_Query_Configured;
        break;
    case EH_HOST_OPENTHREAD_QUERY_INITED:
        option = RPC_FEATURE_OPTION__Feature_Option_Query_Inited;
        break;
    case EH_HOST_OPENTHREAD_QUERY_ENABLED:
        option = RPC_FEATURE_OPTION__Feature_Option_Query_Enabled;
        break;
    case EH_HOST_OPENTHREAD_QUERY_READY:
        option = RPC_FEATURE_OPTION__Feature_Option_Query_Ready;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return rcp_ctrl(RPC_FEATURE_COMMAND__Feature_Command_Query, option);
}

#endif /* EH_HOST_FEAT_OPENTHREAD_READY */
