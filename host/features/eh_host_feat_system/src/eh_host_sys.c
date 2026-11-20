/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* V2 impls of eh_host_sys.h wrappers. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_mac.h"

#include "eh_host_feat_rpc_ext_v2.h"
#include "eh_host_feat_rpc_ext_v2_types.h"
#include "eh_host_feat_rpc.h"
#include "eh_host_feat_system.h"
#include "gen_v2.h"

#include "eh_host_sys.h"
#include "eh_host_event.h"
#include "esp_event.h"

#if EH_HOST_USB_RNDIS
#include "eh_host_mcu_transport.h"
#endif

#if EH_HOST_FEAT_SYSTEM_READY

esp_err_t eh_host_sys_get_cp_fw_version(eh_host_coprocessor_fwver_t *ver_info)
{
    if (!ver_info) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(ver_info, 0, sizeof(*ver_info));

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_GetCoprocessorFwVersion,
                                         req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    if (r->resp_event_status != 0) {
        eh_rpc_ctrl_cmd_free(r);
        return ESP_FAIL;
    }

    ver_info->major1     = r->u.fw_version.major;
    ver_info->minor1     = r->u.fw_version.minor;
    ver_info->patch1     = r->u.fw_version.patch;
    ver_info->revision   = r->u.fw_version.revision;
    ver_info->prerelease = r->u.fw_version.prerelease;
    ver_info->build      = r->u.fw_version.build;

    eh_rpc_ctrl_cmd_free(r);
    return ESP_OK;
}

esp_err_t eh_host_sys_get_cp_info(uint32_t *chip_id,
                                  char *target_name, size_t target_name_len)
{
    if (!chip_id || !target_name || target_name_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_GetCoprocessorFwVersion,
                                         req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    if (r->resp_event_status != 0) {
        eh_rpc_ctrl_cmd_free(r);
        return ESP_FAIL;
    }

    *chip_id = r->u.fw_version.chip_id;

    size_t src_len = r->u.fw_version.idf_target_len;
    if (src_len >= target_name_len) src_len = target_name_len - 1;
    memcpy(target_name, r->u.fw_version.idf_target, src_len);
    target_name[src_len] = '\0';

    eh_rpc_ctrl_cmd_free(r);
    return ESP_OK;
}

esp_err_t eh_host_sys_get_mac(wifi_interface_t mode, uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;
    req->u.wifi_mac.mode = (int32_t)mode;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_GetMACAddress,
                                         req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    if (r->resp_event_status != 0) {
        eh_rpc_ctrl_cmd_free(r);
        return ESP_FAIL;
    }
    memcpy(mac, r->u.wifi_mac.mac, 6);
#if EH_HOST_USB_RNDIS
    if (mode == WIFI_IF_AP) {
        eh_host_mcu_transport_usb_rndis_set_ap_mac(mac);
    }
#endif
    eh_rpc_ctrl_cmd_free(r);
    return ESP_OK;
}

esp_err_t eh_host_sys_set_mac(wifi_interface_t mode, const uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;
    req->u.wifi_mac.mode = (int32_t)mode;
    memcpy(req->u.wifi_mac.mac, mac, 6);

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_SetMacAddress,
                                         req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    int status = r->resp_event_status;
    eh_rpc_ctrl_cmd_free(r);
#if EH_HOST_USB_RNDIS
    if (status == ESP_OK && mode == WIFI_IF_AP) {
        eh_host_mcu_transport_usb_rndis_set_ap_mac(mac);
    }
#endif
    return (esp_err_t)status;
}

/* Mirrors IDF esp_mac_addr_len_get(). */
static size_t iface_mac_addr_required_len(esp_mac_type_t type)
{
    switch (type) {
    case ESP_MAC_IEEE802154: return 8;
    case ESP_MAC_EFUSE_EXT:  return 2;
    default:                 return 6;
    }
}

/* Returns 0 for unrecognized types so apps can detect junk. */
size_t eh_host_iface_mac_addr_len_get(esp_mac_type_t type)
{
    switch (type) {
    case ESP_MAC_IEEE802154: return 8;
    case ESP_MAC_EFUSE_EXT:  return 2;
    case ESP_MAC_WIFI_STA:
    case ESP_MAC_WIFI_SOFTAP:
    case ESP_MAC_BT:
    case ESP_MAC_ETH:
    case ESP_MAC_BASE:
    case ESP_MAC_EFUSE_FACTORY:
    case ESP_MAC_EFUSE_CUSTOM:
        return 6;
    default:
        return 0;
    }
}

esp_err_t eh_host_iface_mac_addr_set(uint8_t *mac, size_t mac_len, esp_mac_type_t type)
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    size_t need = iface_mac_addr_required_len(type);
    if (mac_len < need)                              return ESP_ERR_INVALID_ARG;
    if (need    > sizeof(((eh_rpc_iface_mac_addr_t *)0)->mac)) return ESP_ERR_INVALID_ARG;

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;

    req->u.iface_mac_addr.set     = 1;
    req->u.iface_mac_addr.type    = (uint32_t)type;
    req->u.iface_mac_addr.mac_len = (uint32_t)need;
    memcpy(req->u.iface_mac_addr.mac, mac, need);

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_IfaceMacAddrSetGet,
                                         req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    int status = r->resp_event_status;
    eh_rpc_ctrl_cmd_free(r);
    return (esp_err_t)status;
}

esp_err_t eh_host_iface_mac_addr_get(uint8_t *mac, size_t mac_len, esp_mac_type_t type)
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    size_t need = iface_mac_addr_required_len(type);
    if (mac_len < need) return ESP_ERR_INVALID_ARG;

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;

    req->u.iface_mac_addr.set     = 0;
    req->u.iface_mac_addr.type    = (uint32_t)type;
    req->u.iface_mac_addr.mac_len = 0;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_IfaceMacAddrSetGet,
                                         req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    if (r->resp_event_status != 0) {
        int status = r->resp_event_status;
        eh_rpc_ctrl_cmd_free(r);
        return (esp_err_t)status;
    }
    size_t n = r->u.iface_mac_addr.mac_len;
    if (n > mac_len) n = mac_len;
    if (n) memcpy(mac, r->u.iface_mac_addr.mac, n);
    eh_rpc_ctrl_cmd_free(r);
    return ESP_OK;
}

esp_err_t eh_host_sys_get_cp_app_desc(esp_hosted_app_desc_t *app_desc)
{
    if (!app_desc) return ESP_ERR_INVALID_ARG;
    memset(app_desc, 0, sizeof(*app_desc));

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_AppGetDesc,
                                         req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    /* The CP's error, e.g. ESP_ERR_NOT_SUPPORTED from a CP without the handler. */
    esp_err_t st = (esp_err_t)r->resp_event_status;
    if (st != ESP_OK) {
        eh_rpc_ctrl_cmd_free(r);
        return st;
    }

    const eh_rpc_app_desc_t *d = &r->u.app_desc;
    /* 3.0 and 3.1 CPs send magic_word only with the full descriptor. */
    app_desc->magic_word             = d->magic_word ? d->magic_word : ESP_HOSTED_APP_DESC_MAGIC_WORD;
    app_desc->secure_version         = d->secure_version;
    app_desc->min_efuse_blk_rev_full = (uint16_t)d->min_efuse_blk_rev_full;
    app_desc->max_efuse_blk_rev_full = (uint16_t)d->max_efuse_blk_rev_full;
    app_desc->mmu_page_size          = (uint8_t)d->mmu_page_size;
    memcpy(app_desc->version,      d->version,      sizeof(app_desc->version));
    memcpy(app_desc->project_name, d->project_name, sizeof(app_desc->project_name));
    memcpy(app_desc->time,         d->time,         sizeof(app_desc->time));
    memcpy(app_desc->date,         d->date,         sizeof(app_desc->date));
    memcpy(app_desc->idf_ver,      d->idf_ver,      sizeof(app_desc->idf_ver));
    memcpy(app_desc->app_elf_sha256, d->app_elf_sha256, d->app_elf_sha256_len);

    eh_rpc_ctrl_cmd_free(r);
    return ESP_OK;
}

static void cp_init_event_handler(const void *ctrl_cmd, void *ctx)
{
    (void)ctx;
    if (!ctrl_cmd) return;
    const eh_rpc_ctrl_cmd_t *c = (const eh_rpc_ctrl_cmd_t *)ctrl_cmd;

    eh_host_event_init_t evt = {
        .reason = (esp_reset_reason_t)c->u.e_init.cp_reset_reason,
    };
    esp_event_post(EH_HOST_EVENT,
                             (int32_t)EH_HOST_EVENT_CP_INIT,
                             &evt, sizeof(evt), 0);
}

esp_err_t eh_host_sys_register_event_handlers(void)
{
    if (eh_host_feat_rpc_register_event(RPC_ID__Event_ESPInit,
                                           cp_init_event_handler, NULL) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t eh_host_sys_unregister_event_handlers(void)
{
    eh_host_feat_rpc_unregister_event(RPC_ID__Event_ESPInit,
                                               cp_init_event_handler, NULL);
    return ESP_OK;
}

#endif /* EH_HOST_FEAT_SYSTEM_READY */
