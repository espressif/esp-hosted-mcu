/* SPDX-License-Identifier: Apache-2.0 */
/* System feature response + event parsers. */

#include <string.h>

#include "rpc_ext_v2_decode_priv.h"

#if EH_HOST_FEAT_SYSTEM_READY

/* Copies a bytes field into a char array, always NUL-terminated. */
static void copy_str(char *dst, size_t cap, const ProtobufCBinaryData *src)
{
    size_t n = (src->data && src->len) ? src->len : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src->data, n);
    dst[n] = '\0';
}

int rpc_ext_v2_parse_resp_system(const Rpc *rpc, eh_rpc_ctrl_cmd_t *c)
{
    switch (rpc->msg_id) {
    case RPC_ID__Resp_GetMACAddress:
        if (!rpc->resp_get_mac_address) return -1;
        c->resp_event_status = rpc->resp_get_mac_address->resp;
        EH_RPC_COPY_BIN(c->u.wifi_mac.mac, EH_RPC_MAC_LEN,
                 &rpc->resp_get_mac_address->mac);
        return 0;

    case RPC_ID__Resp_SetMacAddress:
        if (!rpc->resp_set_mac_address) return -1;
        c->resp_event_status = rpc->resp_set_mac_address->resp;
        return 0;

    case RPC_ID__Resp_IfaceMacAddrSetGet: {
        RpcRespIfaceMacAddrSetGet *r = rpc->resp_iface_mac_addr_set_get;
        if (!r) return -1;
        c->resp_event_status         = r->resp;
        c->u.iface_mac_addr.set      = r->set ? 1 : 0;
        c->u.iface_mac_addr.type     = r->type;
        size_t n = r->mac.len;
        if (n > sizeof(c->u.iface_mac_addr.mac)) n = sizeof(c->u.iface_mac_addr.mac);
        memset(c->u.iface_mac_addr.mac, 0, sizeof(c->u.iface_mac_addr.mac));
        if (n) memcpy(c->u.iface_mac_addr.mac, r->mac.data, n);
        c->u.iface_mac_addr.mac_len  = (uint32_t)n;
        return 0;
    }

    case RPC_ID__Resp_GetCoprocessorFwVersion: {
        RpcRespGetCoprocessorFwVersion *r = rpc->resp_get_coprocessor_fwversion;
        if (!r) return -1;
        c->resp_event_status      = r->resp;
        c->u.fw_version.major     = r->major1;
        c->u.fw_version.minor     = r->minor1;
        c->u.fw_version.patch     = r->patch1;
        c->u.fw_version.revision  = r->revision;
        c->u.fw_version.prerelease= r->prerelease;
        c->u.fw_version.build     = r->build;
        c->u.fw_version.chip_id   = r->chip_id;
        size_t tn = r->idf_target.len;
        if (tn >= EH_RPC_IDF_TARGET_LEN) tn = EH_RPC_IDF_TARGET_LEN - 1;
        memcpy(c->u.fw_version.idf_target, r->idf_target.data, tn);
        c->u.fw_version.idf_target[tn] = 0;
        c->u.fw_version.idf_target_len = (uint32_t)tn;
        return 0;
    }

    case RPC_ID__Resp_AppGetDesc: {
        RpcRespAppGetDesc *r = rpc->resp_app_get_desc;
        if (!r) return -1;
        c->resp_event_status = r->resp;
        if (r->resp != 0) return 0;
        const EspAppDesc *a = r->app_desc;
        if (!a) return -1;
        eh_rpc_app_desc_t *d = &c->u.app_desc;
        memset(d, 0, sizeof(*d));
        d->magic_word             = a->magic_word;
        d->secure_version         = a->secure_version;
        d->min_efuse_blk_rev_full = a->min_efuse_blk_rev_full;
        d->max_efuse_blk_rev_full = a->max_efuse_blk_rev_full;
        d->mmu_page_size          = a->mmu_page_size;
        copy_str(d->version,      sizeof(d->version),      &a->version);
        copy_str(d->project_name, sizeof(d->project_name), &a->project_name);
        copy_str(d->time,         sizeof(d->time),         &a->time);
        copy_str(d->date,         sizeof(d->date),         &a->date);
        copy_str(d->idf_ver,      sizeof(d->idf_ver),      &a->idf_ver);
        size_t n = a->app_elf_sha256.len;
        if (n > sizeof(d->app_elf_sha256)) n = sizeof(d->app_elf_sha256);
        EH_RPC_COPY_BIN(d->app_elf_sha256, n, &a->app_elf_sha256);
        d->app_elf_sha256_len = (a->app_elf_sha256.data ? (uint32_t)n : 0);
        return 0;
    }

    default:
        return -1;
    }
}

int rpc_ext_v2_parse_event_system(const Rpc *rpc, eh_rpc_ctrl_cmd_t *c)
{
    switch (rpc->msg_id) {
    case RPC_ID__Event_ESPInit:
        if (!rpc->event_esp_init) return -1;
        c->u.e_init.cp_reset_reason = rpc->event_esp_init->cp_reset_reason;
        return 0;

    default:
        return -1;
    }
}

#endif /* EH_HOST_FEAT_SYSTEM_READY */
