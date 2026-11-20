/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Off-target test of the host AppGetDesc response parser. */

#include <stdio.h>
#include <string.h>

#include "eh_host_port_master_config.h"
#include "rpc_ext_v2_decode_priv.h"

static int s_fail;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_fail++; } \
} while (0)

static ProtobufCBinaryData bin(const void *p, size_t n)
{
    ProtobufCBinaryData b = { .len = n, .data = (uint8_t *)(uintptr_t)p };
    return b;
}

static Rpc resp(RpcRespAppGetDesc *r)
{
    Rpc rpc;
    memset(&rpc, 0, sizeof(rpc));
    rpc.msg_id = RPC_ID__Resp_AppGetDesc;
    rpc.resp_app_get_desc = r;
    return rpc;
}

/* CP error without a descriptor: the CP's error, not a parse failure. */
static void test_error_without_desc(void)
{
    RpcRespAppGetDesc r = { .resp = 0x101 /* ESP_ERR_NO_MEM */, .app_desc = NULL };
    Rpc rpc = resp(&r);
    eh_rpc_ctrl_cmd_t c;
    memset(&c, 0, sizeof(c));
    CHECK(rpc_ext_v2_parse_resp_system(&rpc, &c) == 0);
    CHECK(c.resp_event_status == 0x101);
}

/* Success without a descriptor is malformed. */
static void test_ok_without_desc(void)
{
    RpcRespAppGetDesc r = { .resp = 0, .app_desc = NULL };
    Rpc rpc = resp(&r);
    eh_rpc_ctrl_cmd_t c;
    memset(&c, 0, sizeof(c));
    CHECK(rpc_ext_v2_parse_resp_system(&rpc, &c) == -1);
}

/* Every field round-trips; strings stay NUL-terminated even at full length. */
static void test_full_fields(void)
{
    uint8_t sha[32];
    for (int i = 0; i < 32; i++) sha[i] = (uint8_t)(0xa0 + i);
    const char ver32[] = "0123456789abcdef0123456789abcdef";   /* 32 chars, no NUL on the wire */
    EspAppDesc a;
    memset(&a, 0, sizeof(a));
    a.magic_word             = 0xABCD5432;
    a.secure_version         = 7;
    a.version                = bin(ver32, 32);
    a.project_name           = bin("proj", 4);
    a.time                   = bin("10:20:30", 8);
    a.date                   = bin("Nov 19 2026", 11);
    a.idf_ver                = bin("v6.1", 4);
    a.app_elf_sha256         = bin(sha, sizeof(sha));
    a.min_efuse_blk_rev_full = 1;
    a.max_efuse_blk_rev_full = 199;
    a.mmu_page_size          = 15;
    RpcRespAppGetDesc r = { .resp = 0, .app_desc = &a };
    Rpc rpc = resp(&r);
    eh_rpc_ctrl_cmd_t c;
    memset(&c, 0xff, sizeof(c));

    CHECK(rpc_ext_v2_parse_resp_system(&rpc, &c) == 0);
    const eh_rpc_app_desc_t *d = &c.u.app_desc;
    CHECK(c.resp_event_status == 0);
    CHECK(d->magic_word == 0xABCD5432);
    CHECK(d->secure_version == 7);
    CHECK(strcmp(d->version, "0123456789abcdef0123456789abcde") == 0);
    CHECK(strcmp(d->project_name, "proj") == 0);
    CHECK(strcmp(d->time, "10:20:30") == 0);
    CHECK(strcmp(d->date, "Nov 19 2026") == 0);
    CHECK(strcmp(d->idf_ver, "v6.1") == 0);
    CHECK(d->app_elf_sha256_len == 32);
    CHECK(memcmp(d->app_elf_sha256, sha, 32) == 0);
    CHECK(d->min_efuse_blk_rev_full == 1);
    CHECK(d->max_efuse_blk_rev_full == 199);
    CHECK(d->mmu_page_size == 15);
}

/* Default CP build: no full-descriptor fields; they parse as zero / empty. */
static void test_default_fields(void)
{
    EspAppDesc a;
    memset(&a, 0, sizeof(a));
    a.magic_word = 0xABCD5432;
    a.version    = bin("1.0", 3);
    RpcRespAppGetDesc r = { .resp = 0, .app_desc = &a };
    Rpc rpc = resp(&r);
    eh_rpc_ctrl_cmd_t c;
    memset(&c, 0xff, sizeof(c));

    CHECK(rpc_ext_v2_parse_resp_system(&rpc, &c) == 0);
    const eh_rpc_app_desc_t *d = &c.u.app_desc;
    CHECK(strcmp(d->version, "1.0") == 0);
    CHECK(d->time[0] == '\0' && d->date[0] == '\0' && d->project_name[0] == '\0');
    CHECK(d->app_elf_sha256_len == 0);
    CHECK(d->mmu_page_size == 0);
}

int main(void)
{
    test_error_without_desc();
    test_ok_without_desc();
    test_full_fields();
    test_default_fields();
    printf("%s\n", s_fail ? "FAILED" : "OK");
    return s_fail ? 1 : 0;
}
