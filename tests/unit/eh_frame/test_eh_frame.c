/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Wire-level checks for eh_frame: byte values that peers depend on. */

#include <stdio.h>
#include <string.h>
#include "eh_frame.h"

static int s_fail;

#define CHECK(cond, ...) do {                                     \
    if (!(cond)) {                                                \
        s_fail++;                                                 \
        printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);    \
        printf(__VA_ARGS__);                                      \
        printf("\n");                                             \
    }                                                             \
} while (0)

#define BUF_SZ 1600

typedef struct {
    eh_if_type_t if_type;
    uint8_t      wire;
} wire_map_t;

#ifdef CONFIG_ESP_HOSTED_LEGACY_WIRE_INTERFACE_TYPES
static const wire_map_t k_map[] = {
    { ESP_STA_IF, 0 }, { ESP_AP_IF, 1 }, { ESP_SERIAL_IF, 2 }, { ESP_HCI_IF, 3 },
    { ESP_PRIV_IF, 4 }, { ESP_TEST_IF, 5 }, { ESP_AUX_IF, 7 },
};
#define DUMMY_WIRE 6u
#else
static const wire_map_t k_map[] = {
    { ESP_STA_IF, 1 }, { ESP_AP_IF, 2 }, { ESP_SERIAL_IF, 3 }, { ESP_HCI_IF, 4 },
    { ESP_PRIV_IF, 5 }, { ESP_TEST_IF, 6 }, { ESP_AUX_IF, 7 },
};
#define DUMMY_WIRE 8u
#endif
#define N_MAP (sizeof(k_map) / sizeof(k_map[0]))

static const uint8_t k_pkt_types[] = { 0x00, 0x01, 0xA5 };
#define N_PKT (sizeof(k_pkt_types) / sizeof(k_pkt_types[0]))

static const uint8_t k_payload[] = { 0x11, 0x22, 0x33, 0x44, 0x55 };

static void use_version(uint8_t ver)
{
    eh_frame_cfg_t cfg = EH_FRAME_CFG_HOST_MCU_SPI_DEFAULT;
    cfg.hdr_version = ver;
    CHECK(eh_frame_init(&cfg) == ESP_OK, "init v%u", ver);
}

static uint16_t build(uint8_t *buf, eh_if_type_t t, uint8_t pkt_type, uint16_t seq)
{
    interface_buffer_handle_t h = { 0 };
    uint8_t hdr;

    h.if_type  = t;
    h.pkt_type = pkt_type;
    h.seq_num  = seq;
    memset(buf, 0, BUF_SZ);
    hdr = eh_frame_hdr_size();
    memcpy(buf + hdr, k_payload, sizeof(k_payload));
    CHECK(eh_frame_encode(buf, &h, sizeof(k_payload)) == hdr, "encode hdr size");
    return (uint16_t)(hdr + sizeof(k_payload));
}

/* V1 byte 0 = if_num:4 | if_type:4; byte 11 = pkt_type. Old peers parse these raw. */
static void test_v1_wire_bytes(void)
{
    uint8_t buf[BUF_SZ];
    size_t i, p;

    use_version(ESP_HOSTED_HDR_VERSION_V1);
    CHECK(eh_frame_hdr_size() == 12, "v1 header is 12 bytes");

    for (i = 0; i < N_MAP; i++) {
        for (p = 0; p < N_PKT; p++) {
            build(buf, k_map[i].if_type, k_pkt_types[p], 0x1234);
            CHECK(buf[0] == k_map[i].wire, "if_type %d: byte0=0x%02x want 0x%02x",
                  k_map[i].if_type, buf[0], k_map[i].wire);
            CHECK(buf[2] == sizeof(k_payload) && buf[3] == 0, "len LE");
            CHECK(buf[4] == 12 && buf[5] == 0, "offset LE");
            CHECK(buf[8] == 0x34 && buf[9] == 0x12, "seq LE");
            CHECK(buf[11] == k_pkt_types[p], "if_type %d: byte11=0x%02x want 0x%02x",
                  k_map[i].if_type, buf[11], k_pkt_types[p]);
            CHECK(buf[0] != ESP_HOSTED_HDR_V2_MAGIC, "v1 data frame looks like v2");
        }
    }
}

/* V2 carries pkt_type in byte 6 (packet_type) and byte 19 (union). */
static void test_v2_wire_bytes(void)
{
    uint8_t buf[BUF_SZ];
    size_t i;

    use_version(ESP_HOSTED_HDR_VERSION_V2);
    CHECK(eh_frame_hdr_size() == 20, "v2 header is 20 bytes");

    for (i = 0; i < N_MAP; i++) {
        build(buf, k_map[i].if_type, 0xA5, 7);
        CHECK(buf[0] == ESP_HOSTED_HDR_V2_MAGIC, "v2 magic");
        CHECK((buf[4] & 0x3F) == k_map[i].wire, "if_type %d: v2 if_type=%u want %u",
              k_map[i].if_type, buf[4] & 0x3F, k_map[i].wire);
        CHECK(buf[6] == 0xA5 && buf[19] == 0xA5, "v2 pkt_type bytes");
    }
}

static void test_round_trip(uint8_t ver)
{
    uint8_t buf[BUF_SZ];
    size_t i, p;

    use_version(ver);
    for (i = 0; i < N_MAP; i++) {
        for (p = 0; p < N_PKT; p++) {
            interface_buffer_handle_t out;
            uint16_t total = build(buf, k_map[i].if_type, k_pkt_types[p], 42);

            memset(&out, 0xEE, sizeof(out));
            CHECK(eh_frame_decode(buf, total, &out) == EH_FRAME_OK,
                  "v%u if_type %d decode", ver, k_map[i].if_type);
            CHECK(out.if_type == k_map[i].if_type, "v%u if_type %d -> %u",
                  ver, k_map[i].if_type, out.if_type);
            CHECK(out.pkt_type == k_pkt_types[p], "v%u pkt_type", ver);
            CHECK(out.seq_num == 42, "v%u seq", ver);
            CHECK(out.payload_len == sizeof(k_payload) &&
                  memcmp(out.payload, k_payload, sizeof(k_payload)) == 0, "v%u payload", ver);
        }
    }
}

static void test_dummy(void)
{
    uint8_t buf[BUF_SZ];
    interface_buffer_handle_t out;

    use_version(ESP_HOSTED_HDR_VERSION_V1);
    CHECK(eh_if_type_dummy_wire() == DUMMY_WIRE, "dummy wire=%u want %u",
          eh_if_type_dummy_wire(), DUMMY_WIRE);
    memset(buf, 0, sizeof(buf));
    CHECK(eh_frame_encode_dummy(buf, 0x2) == 12, "v1 dummy size");
    CHECK(buf[0] == (0xF0 | DUMMY_WIRE), "v1 dummy byte0=0x%02x", buf[0]);
    CHECK((buf[10] & 0x03) == 0x2, "v1 dummy throttle");
    CHECK(eh_frame_decode(buf, 12, &out) == EH_FRAME_DUMMY, "v1 dummy decode");

    use_version(ESP_HOSTED_HDR_VERSION_V2);
    memset(buf, 0, sizeof(buf));
    CHECK(eh_frame_encode_dummy(buf, 0) == 20, "v2 dummy size");
    CHECK((buf[4] & 0x3F) == DUMMY_WIRE, "v2 dummy if_type");
    CHECK(eh_frame_decode(buf, 20, &out) == EH_FRAME_DUMMY, "v2 dummy decode");
}

/* Any frame without payload or flags is idle, whatever its if_type. */
static void test_zero_len_is_dummy(void)
{
    uint8_t buf[BUF_SZ];
    interface_buffer_handle_t h = { 0 }, out;
    size_t i;

    use_version(ESP_HOSTED_HDR_VERSION_V1);
    for (i = 0; i < N_MAP; i++) {
        h.if_type = k_map[i].if_type;
        eh_frame_encode(buf, &h, 0);
        CHECK(eh_frame_decode(buf, 12, &out) == EH_FRAME_DUMMY,
              "if_type %d len 0", k_map[i].if_type);
    }
}

static void test_bad_frames(void)
{
    uint8_t buf[BUF_SZ];
    interface_buffer_handle_t out;
    uint16_t total;
    eh_frame_cfg_t cfg = EH_FRAME_CFG_HOST_MCU_SPI_DEFAULT;

    use_version(ESP_HOSTED_HDR_VERSION_V1);
    total = build(buf, ESP_STA_IF, 0, 1);
    buf[total - 1] ^= 0xFF;
    CHECK(eh_frame_decode_quiet(buf, total, &out) == EH_FRAME_CORRUPT, "checksum");

    cfg.max_buf_size = 14;
    CHECK(eh_frame_init(&cfg) == ESP_OK, "init small");
    total = build(buf, ESP_STA_IF, 0, 1);
    CHECK(eh_frame_decode_quiet(buf, total, &out) == EH_FRAME_TOOBIG, "too big");
}

int main(void)
{
    test_v1_wire_bytes();
    test_v2_wire_bytes();
    test_round_trip(ESP_HOSTED_HDR_VERSION_V1);
    test_round_trip(ESP_HOSTED_HDR_VERSION_V2);
    test_dummy();
    test_zero_len_is_dummy();
    test_bad_frames();

    printf("%s\n", s_fail ? "FAILED" : "OK");
    return s_fail ? 1 : 0;
}
