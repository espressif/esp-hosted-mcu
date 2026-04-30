/* host/core/include/h_internal/h_priv_event.h
 *
 * Platform-independent TLV handshake protocol definitions.
 * Replaces esp_private/wifi.h in the existing codebase.
 *
 * After slave powers on, host and slave exchange TLV frames to negotiate
 * capabilities, chip ID, firmware version, queue sizes, and transport mode.
 * This protocol is specific to ESP-Hosted's custom transport layer. */

#ifndef H_PRIV_EVENT_H
#define H_PRIV_EVENT_H

#include <stdint.h>

/* ── Private Event Frame ── */
typedef struct {
    uint8_t  event_type;
    uint8_t *tlv_data;
    uint16_t tlv_len;
} h_priv_event_t;

/* ── Event Types ── */
#define H_PRIV_EVENT_INIT              0x01
#define H_PRIV_CAPABILITY              0x10
#define H_PRIV_CAP_EXT                 0x11
#define H_PRIV_FIRMWARE_CHIP_ID        0x20
#define H_PRIV_FIRMWARE_CHIP_ESP32     0x00
#define H_PRIV_FIRMWARE_CHIP_ESP32S2   0x01
#define H_PRIV_FIRMWARE_CHIP_ESP32C2   0x02
#define H_PRIV_FIRMWARE_CHIP_ESP32C3   0x03
#define H_PRIV_FIRMWARE_CHIP_ESP32C6   0x04
#define H_PRIV_FIRMWARE_CHIP_ESP32C5   0x05
#define H_PRIV_FIRMWARE_CHIP_ESP32S3   0x06
#define H_PRIV_FIRMWARE_CHIP_ESP32H2   0x07
#define H_PRIV_FIRMWARE_CHIP_ESP32H4   0x08
#define H_PRIV_FIRMWARE_CHIP_ESP32C61  0x09
#define H_PRIV_FIRMWARE_CHIP_ESP32P4   0x0A
#define H_PRIV_TEST_RAW_TP             0x30
#define H_PRIV_RX_Q_SIZE               0x40
#define H_PRIV_TX_Q_SIZE               0x41
#define H_PRIV_FIRMWARE_VERSION        0x50
#define H_PRIV_TRANS_SDIO_MODE         0x60

#endif /* H_PRIV_EVENT_H */
