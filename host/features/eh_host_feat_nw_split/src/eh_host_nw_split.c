/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "eh_host_feat_rpc_ext_v2.h"
#include "eh_host_feat_rpc_ext_v2_types.h"
#include "eh_host_feat_rpc.h"
#include "gen_v2.h"
#include "eh_common_ctx.h"

#include "eh_host_port.h"
#include "esp_event.h"

#include "eh_host_event.h"
#include "eh_host_nw_split.h"
#include "eh_host_nw_split_lease_fsm.h"
#include "eh_host_sta_fsm.h"

#include "eh_host_feat_rpc_ext_v2_api_common.h"

#if EH_HOST_FEAT_NW_SPLIT_READY

/* WIFI_IF_STA == 0 in the CP's proto. */
#define NW_SPLIT_DEFAULT_IFACE  0

/* LE-only: result has memory byte[0] == first octet, matching esp_ip4_addr_t.addr. */
static bool nw_split_parse_ipv4_text(const char *s, uint32_t *out_addr)
{
    if (!s || !out_addr) return false;

    char *end = NULL;
    unsigned long a = strtoul(s, &end, 10);
    if (!end || *end != '.') return false;
    unsigned long b = strtoul(end + 1, &end, 10);
    if (!end || *end != '.') return false;
    unsigned long c = strtoul(end + 1, &end, 10);
    if (!end || *end != '.') return false;
    unsigned long d = strtoul(end + 1, &end, 10);
    if (!end || *end != '\0') return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;

    *out_addr = ((uint32_t)a)        |
                ((uint32_t)b <<  8)  |
                ((uint32_t)c << 16)  |
                ((uint32_t)d << 24);
    return true;
}

/* CP wire carries dotted-quad ASCII (current) or 4-byte binary (legacy). */
static uint32_t nw_split_blob_to_ip4_addr(const eh_rpc_blob_t *blob)
{
    if (!blob || !blob->data || blob->len == 0) {
        return 0;
    }

    if (blob->len == 4) {
        uint32_t v;
        memcpy(&v, blob->data, 4);
        return v;
    }

    char txt[32];
    size_t n = (blob->len < sizeof(txt) - 1) ? blob->len : (sizeof(txt) - 1);
    memcpy(txt, blob->data, n);
    txt[n] = '\0';

    uint32_t addr = 0;
    if (nw_split_parse_ipv4_text(txt, &addr)) {
        return addr;
    }
    return 0;
}

/* NULL/empty src leaves dst zeroed (matches "field not set" wire semantic). */
static int nw_split_blob_put_cstr(eh_rpc_blob_t *dst, const char *src)
{
    size_t n = (src && *src) ? strlen(src) : 0;
    return eh_rpc_blob_take(dst, (const uint8_t *)src, n);
}

/* v1 wire is v4-only: has_ipv6/dns_backup/ipv6 left zero. */
static void nw_split_fill_status_from_wire(eh_host_nw_split_status_t *out,
                                           const eh_rpc_blob_t *dhcp_ip,
                                           const eh_rpc_blob_t *dhcp_nm,
                                           const eh_rpc_blob_t *dhcp_gw,
                                           const eh_rpc_blob_t *dns_ip,
                                           uint8_t net_link_up,
                                           uint8_t dhcp_up)
{
    memset(out, 0, sizeof(*out));
    out->link_up = (net_link_up != 0);
    out->dhcp_up = (dhcp_up != 0);

    uint32_t ip_addr = nw_split_blob_to_ip4_addr(dhcp_ip);
    if (out->dhcp_up && ip_addr) {
        out->has_ipv4              = true;
        out->ipv4.ip.addr          = ip_addr;
        out->ipv4.netmask.addr     = nw_split_blob_to_ip4_addr(dhcp_nm);
        out->ipv4.gw.addr          = nw_split_blob_to_ip4_addr(dhcp_gw);
    }

    uint32_t dns_addr = nw_split_blob_to_ip4_addr(dns_ip);
    if (dns_addr) {
        out->dns_main.type             = ESP_IPADDR_TYPE_V4;
        out->dns_main.u_addr.ip4.addr  = dns_addr;
    }
}

static void nw_split_event_handler(const void *ctrl_cmd, void *ctx)
{
    (void)ctx;
    if (!ctrl_cmd) return;
    const eh_rpc_ctrl_cmd_t *c = (const eh_rpc_ctrl_cmd_t *)ctrl_cmd;

    eh_host_nw_split_status_t evt;
    nw_split_fill_status_from_wire(&evt,
                                   &c->u.dhcp_dns.dhcp_ip,
                                   &c->u.dhcp_dns.dhcp_nm,
                                   &c->u.dhcp_dns.dhcp_gw,
                                   &c->u.dhcp_dns.dns_ip,
                                   c->u.dhcp_dns.net_link_up,
                                   c->u.dhcp_dns.dhcp_up);
    /* Here, as the CP's link reports: a lease never overtakes the drop sent after it. */
    if (evt.link_up && evt.dhcp_up && evt.has_ipv4 && evt.ipv4.ip.addr != 0) {
        eh_host_sta_fsm_report(EH_STA_EV_LEASE_READY, NULL, 0);
    }
    esp_event_post(EH_HOST_EVENT,
                             (int32_t)EH_HOST_EVENT_NW_SPLIT_STATUS,
                             &evt, sizeof(evt), 0);
}

esp_err_t eh_host_nw_split_get_status(eh_host_nw_split_status_t *out)
{
    if (!out) {
        return ESP_FAIL;
    }

    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return ESP_FAIL;
    req->u.dhcp_dns.iface = NW_SPLIT_DEFAULT_IFACE;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_GetDhcpDnsStatus, req,
                                         (void **)&r) != 0) {
        return ESP_FAIL;
    }
    int status = r->resp_event_status;
    if (status == 0) {
        nw_split_fill_status_from_wire(out,
                                       &r->u.dhcp_dns.dhcp_ip,
                                       &r->u.dhcp_dns.dhcp_nm,
                                       &r->u.dhcp_dns.dhcp_gw,
                                       &r->u.dhcp_dns.dns_ip,
                                       r->u.dhcp_dns.net_link_up,
                                       r->u.dhcp_dns.dhcp_up);
    } else {
        memset(out, 0, sizeof(*out));
    }
    eh_rpc_ctrl_cmd_free(r);
    return (esp_err_t)status;
}

static eh_rpc_ctrl_cmd_t *nw_split_status_req(uint32_t iface,
                                              uint8_t link_up, uint8_t dhcp_up,
                                              const char *dhcp_ip,
                                              const char *dhcp_nm,
                                              const char *dhcp_gw,
                                              uint8_t dns_up,
                                              const char *dns_ip,
                                              uint8_t dns_type)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) return NULL;
    req->u.dhcp_dns.iface       = iface;
    req->u.dhcp_dns.net_link_up = link_up;
    req->u.dhcp_dns.dhcp_up     = dhcp_up;
    req->u.dhcp_dns.dns_up      = dns_up;
    req->u.dhcp_dns.dns_type    = dns_type;
    if (nw_split_blob_put_cstr(&req->u.dhcp_dns.dhcp_ip, dhcp_ip) != 0 ||
        nw_split_blob_put_cstr(&req->u.dhcp_dns.dhcp_nm, dhcp_nm) != 0 ||
        nw_split_blob_put_cstr(&req->u.dhcp_dns.dhcp_gw, dhcp_gw) != 0 ||
        nw_split_blob_put_cstr(&req->u.dhcp_dns.dns_ip,  dns_ip)  != 0) {
        if (req->u.dhcp_dns.dhcp_ip.data) free(req->u.dhcp_dns.dhcp_ip.data);
        if (req->u.dhcp_dns.dhcp_nm.data) free(req->u.dhcp_dns.dhcp_nm.data);
        if (req->u.dhcp_dns.dhcp_gw.data) free(req->u.dhcp_dns.dhcp_gw.data);
        if (req->u.dhcp_dns.dns_ip.data)  free(req->u.dhcp_dns.dns_ip.data);
        free(req);
        return NULL;
    }
    return req;
}

esp_err_t eh_host_nw_split_set_status(uint32_t iface,
                                      uint8_t link_up, uint8_t dhcp_up,
                                      const char *dhcp_ip,
                                      const char *dhcp_nm,
                                      const char *dhcp_gw,
                                      uint8_t dns_up,
                                      const char *dns_ip,
                                      uint8_t dns_type)
{
    eh_rpc_ctrl_cmd_t *req = nw_split_status_req(iface, link_up, dhcp_up, dhcp_ip, dhcp_nm,
                                                 dhcp_gw, dns_up, dns_ip, dns_type);
    if (!req) return ESP_FAIL;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_SetDhcpDnsStatus, req,
                                         (void **)&r) != 0) {
        return ESP_FAIL;
    }
    int status = r->resp_event_status;
    eh_rpc_ctrl_cmd_free(r);
    return (esp_err_t)status;
}

static void nw_split_status_done(void *resp, void *ctx, int status)
{
    eh_host_nw_split_done_cb_t done = (eh_host_nw_split_done_cb_t)ctx;
    eh_rpc_ctrl_cmd_t *r = (eh_rpc_ctrl_cmd_t *)resp;
    esp_err_t rc = (status == EH_RPC_STATUS_OK && r) ? (esp_err_t)r->resp_event_status : ESP_FAIL;

    if (r) {
        eh_rpc_ctrl_cmd_free(r);
    }
    if (done) {
        done(rc);
    }
}

esp_err_t eh_host_nw_split_set_status_async(const eh_host_nw_split_status_t *st,
                                            eh_host_nw_split_done_cb_t done)
{
    char ip[16] = "0.0.0.0", nm[16] = "0.0.0.0", gw[16] = "0.0.0.0", dns[16] = "0.0.0.0";
    bool dns_v4 = st->dns_main.type == ESP_IPADDR_TYPE_V4 && st->dns_main.u_addr.ip4.addr;

    if (st->has_ipv4) {
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&st->ipv4.ip));
        snprintf(nm, sizeof(nm), IPSTR, IP2STR(&st->ipv4.netmask));
        snprintf(gw, sizeof(gw), IPSTR, IP2STR(&st->ipv4.gw));
    }
    if (dns_v4) {
        snprintf(dns, sizeof(dns), IPSTR, IP2STR(&st->dns_main.u_addr.ip4));
    }
    eh_rpc_ctrl_cmd_t *req = nw_split_status_req(0 /* STA */, st->link_up, st->dhcp_up, ip, nm,
                                                 gw, st->has_ipv4, dns, ESP_NETIF_DNS_MAIN);
    if (!req) return ESP_FAIL;

    req->msg_id         = RPC_ID__Req_SetDhcpDnsStatus;
    req->rpc_rsp_cb     = nw_split_status_done;
    req->rpc_rsp_cb_ctx = (void *)done;
    return eh_host_feat_rpc_request(RPC_ID__Req_SetDhcpDnsStatus, req, 0, NULL) == 0 ?
           ESP_OK : ESP_FAIL;
}

static eh_rpc_ctrl_cmd_t *ip_owner_req(uint8_t want)
{
    const eh_ctx_ip_owner_req_t rq = { .iface = NW_SPLIT_DEFAULT_IFACE, .owner = want };
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();

    if (!req) return NULL;
    req->msg_id     = RPC_ID__Req_SetContext;
    req->u.ctx.type = EH_CTX_NW_SPLIT_IP_OWNER;
    req->u.ctx.len  = sizeof(rq);
    memcpy(req->u.ctx.data, &rq, sizeof(rq));
    return req;
}

/* The CP's answer, or its own change: false when it is not an IP-owner context. */
static bool ip_owner_of(const eh_rpc_ctrl_cmd_t *c, eh_ctx_ip_owner_t *a)
{
    return c && c->u.ctx.type == EH_CTX_NW_SPLIT_IP_OWNER &&
           eh_ctx_unpack(a, sizeof(*a), sizeof(a->owner), c->u.ctx.data, c->u.ctx.len);
}

esp_err_t eh_host_nw_split_rpc_set_ip_owner(uint8_t want, uint8_t *owner, bool *pending)
{
    eh_rpc_ctrl_cmd_t *req = ip_owner_req(want);
    eh_rpc_ctrl_cmd_t *r = NULL;
    eh_ctx_ip_owner_t a = {0};

    if (!req) return ESP_FAIL;
    if (eh_host_feat_rpc_request_sync(RPC_ID__Req_SetContext, req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    esp_err_t rc = (esp_err_t)r->resp_event_status;
    if (!ip_owner_of(r, &a) && rc == ESP_OK) {
        rc = ESP_FAIL;
    }
    *owner = a.owner;
    *pending = a.pending;
    eh_rpc_ctrl_cmd_free(r);
    return rc;
}

static void nw_split_owner_done(void *resp, void *ctx, int status)
{
    eh_host_nw_split_owner_cb_t done = (eh_host_nw_split_owner_cb_t)ctx;
    eh_rpc_ctrl_cmd_t *r = (eh_rpc_ctrl_cmd_t *)resp;
    esp_err_t rc = (status == EH_RPC_STATUS_OK && r) ? (esp_err_t)r->resp_event_status : ESP_FAIL;
    eh_ctx_ip_owner_t a = {0};

    /* A refusal carries the owner in effect too. */
    if (!ip_owner_of(r, &a) && rc == ESP_OK) {
        rc = ESP_FAIL;
    }
    if (r) {
        eh_rpc_ctrl_cmd_free(r);
    }
    done(rc, a.owner, a.pending);
}

esp_err_t eh_host_nw_split_rpc_set_ip_owner_async(uint8_t want, eh_host_nw_split_owner_cb_t done)
{
    eh_rpc_ctrl_cmd_t *req = ip_owner_req(want);

    if (!req) return ESP_FAIL;
    req->rpc_rsp_cb     = nw_split_owner_done;
    req->rpc_rsp_cb_ctx = (void *)done;
    return eh_host_feat_rpc_request(RPC_ID__Req_SetContext, req, 0, NULL) == 0 ?
           ESP_OK : ESP_FAIL;
}

static void nw_split_owner_event_handler(const void *ctrl_cmd, void *ctx)
{
    eh_ctx_ip_owner_t a;

    (void)ctx;
    if (ip_owner_of((const eh_rpc_ctrl_cmd_t *)ctrl_cmd, &a)) {
        eh_host_nw_split_lease_fsm_cp_owner((eh_host_nw_split_ip_owner_t)a.owner);
    }
}

esp_err_t eh_host_nw_split_register_event_handlers(void)
{
    int rc = eh_host_feat_rpc_register_event(RPC_ID__Event_DhcpDnsStatus,
                                             nw_split_event_handler, NULL);

    rc |= eh_host_feat_rpc_register_event(RPC_ID__Event_Context,
                                          nw_split_owner_event_handler, NULL);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t eh_host_nw_split_unregister_event_handlers(void)
{
    eh_host_feat_rpc_unregister_event(
        RPC_ID__Event_DhcpDnsStatus, nw_split_event_handler, NULL);
    eh_host_feat_rpc_unregister_event(
        RPC_ID__Event_Context, nw_split_owner_event_handler, NULL);
    return ESP_OK;
}

#endif /* EH_HOST_FEAT_NW_SPLIT_READY */
