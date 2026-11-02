/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Back-compat aliases for upstream-MCU OpenThread type + API names. */

#ifndef __ESP_HOSTED_OPENTHREAD_H__
#define __ESP_HOSTED_OPENTHREAD_H__

#include "eh_host_openthread.h"
#include "eh_host_openthread_transport.h"
#include "eh_host_port_openthread.h"

#define esp_hosted_openthread_query_t             eh_host_openthread_query_t

#define HOSTED_OPENTHREAD_QUERY_CONFIGURED        EH_HOST_OPENTHREAD_QUERY_CONFIGURED
#define HOSTED_OPENTHREAD_QUERY_INITED            EH_HOST_OPENTHREAD_QUERY_INITED
#define HOSTED_OPENTHREAD_QUERY_ENABLED           EH_HOST_OPENTHREAD_QUERY_ENABLED
#define HOSTED_OPENTHREAD_QUERY_READY             EH_HOST_OPENTHREAD_QUERY_READY

#define ESP_HOSTED_OPENTHREAD_RADIO_CONFIG()       EH_HOST_PORT_OPENTHREAD_RADIO_CONFIG()
#define esp_hosted_openthread_rcp_init()           eh_host_openthread_rcp_init()
#define esp_hosted_openthread_rcp_deinit()         eh_host_openthread_rcp_deinit()
#define esp_hosted_openthread_rcp_start()          eh_host_openthread_rcp_start()
#define esp_hosted_openthread_rcp_stop()           eh_host_openthread_rcp_stop()
#define esp_hosted_openthread_rcp_query(q)         eh_host_openthread_rcp_query(q)

#endif /* __ESP_HOSTED_OPENTHREAD_H__ */
