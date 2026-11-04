/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_host_port_master_config.h"

#ifdef ESP_PLATFORM
#if EH_HOST_TRANSPORT_BUS_USB

#if !EH_HOST_USB_STACK_CHERRYUSB
#error "eh_host_bus_usb_cherryusb.c requires ESP_HOSTED_HOST_USB_STACK_CHERRYUSB"
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "eh_host_port.h"
#include "usbh_core.h"
#include "usbh_serial.h"
#include "eh_host_bus_usb_priv.h"

static const char *TAG = "eh_bus_usb";

/* One bulk packet. FS MPS is 64 and HS bulk MPS is 512, so 512 is a multiple of both. */
#define EH_CHERRY_RX_LEN 512
#define EH_CHERRY_OPEN_POLL_MS 50

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_cherry_rx_buf[USB_ALIGN_UP(EH_CHERRY_RX_LEN, CONFIG_USB_ALIGN_SIZE)];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_cherry_tx_buf[USB_ALIGN_UP(EH_USB_MAX_BUF, CONFIG_USB_ALIGN_SIZE)];

static struct usbh_serial *s_cherry_serial = NULL;
static eh_host_port_sem_t *s_cherry_rx_sem = NULL;
static eh_host_port_sem_t *s_cherry_tx_sem = NULL;
static eh_host_port_task_t *s_cherry_rx_pump = NULL;
static volatile int s_cherry_rx_nbytes = 0;
static volatile int s_cherry_tx_nbytes = 0;
static volatile uint8_t s_cherry_closing = 0;
static volatile uint8_t s_cherry_rx_run = 0;
static volatile uint8_t s_cherry_host_up = 0;
static volatile uint8_t s_cdc_opened = 0;

static void cherry_stack_close(void);

/* Hub thread calls this on class removal. Ignored while the host is closing. */
void usbh_serial_stop(struct usbh_serial *serial)
{
    (void)serial;
    if (s_cherry_closing || !s_cdc_opened) {
        return;
    }
    s_cdc_opened = 0;
    s_cherry_rx_run = 0;
    if (s_cherry_rx_sem) {
        eh_host_port_sem_post(s_cherry_rx_sem);
    }
    eh_host_usb_report_disconnect();
}

static void cherry_rx_complete(void *arg, int nbytes)
{
    (void)arg;
    s_cherry_rx_nbytes = nbytes;
    if (s_cherry_rx_sem) {
        eh_host_port_sem_post_from_isr(s_cherry_rx_sem);
    }
}

static void cherry_tx_complete(void *arg, int nbytes)
{
    (void)arg;
    s_cherry_tx_nbytes = nbytes;
    if (s_cherry_tx_sem) {
        eh_host_port_sem_post_from_isr(s_cherry_tx_sem);
    }
}

/* Copies a completed bulk IN into the frame stream, then re-arms the transfer. */
static void cherry_rx_pump(void *arg)
{
    (void)arg;

    while (s_cherry_rx_run) {
        if (eh_host_port_sem_wait_ms(s_cherry_rx_sem, 100) != EH_HOST_PORT_OK) {
            continue;
        }
        if (!s_cherry_rx_run || s_cherry_closing) {
            break;
        }

        int nbytes = s_cherry_rx_nbytes;
        if (nbytes > 0) {
            eh_host_usb_push_rx(s_cherry_rx_buf, (size_t)nbytes);
        } else if (nbytes < 0) {
            ESP_LOGW(TAG, "CherryUSB RX error %d", nbytes);
            continue;
        }

        if (!s_cherry_serial || !s_cdc_opened || s_cherry_closing) {
            break;
        }
        int ret = usbh_serial_cdc_read_async(
            s_cherry_serial, s_cherry_rx_buf, EH_CHERRY_RX_LEN, cherry_rx_complete, NULL);
        if (ret < 0) {
            ESP_LOGW(TAG, "cdc read submit failed: %d", ret);
            eh_host_port_task_delay_ms(10);
        }
    }
}

static void cherry_stop_rx_pump(void)
{
    s_cherry_rx_run = 0;
    if (s_cherry_rx_sem) {
        eh_host_port_sem_post(s_cherry_rx_sem);
    }
    if (s_cherry_rx_pump) {
        eh_host_port_task_join(s_cherry_rx_pump);
        eh_host_port_task_destroy(s_cherry_rx_pump);
        s_cherry_rx_pump = NULL;
    }
}

static int cherry_stack_start(void)
{
    int ret = usbh_initialize(0, (uintptr_t)ESP_USB_HS0_BASE, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "usbh_initialize failed: %d", ret);
        return -1;
    }
    s_cherry_host_up = 1;

    s_cherry_rx_sem = eh_host_port_sem_create();
    s_cherry_tx_sem = eh_host_port_sem_create();
    if (!s_cherry_rx_sem || !s_cherry_tx_sem) {
        ESP_LOGE(TAG, "failed to create CherryUSB sync objects");
        cherry_stack_close();
        return -1;
    }
    return 0;
}

static int cherry_stack_open(void)
{
    const struct eh_host_usb_otg_config *cfg = eh_host_usb_cfg();
    uint32_t waited = 0;
    while (waited < cfg->connection_timeout_ms) {
        s_cherry_serial = usbh_serial_open("/dev/ttyACM0", USBH_SERIAL_O_RDWR);
        if (s_cherry_serial) {
            break;
        }
        eh_host_port_task_delay_ms(EH_CHERRY_OPEN_POLL_MS);
        waited += EH_CHERRY_OPEN_POLL_MS;
    }
    if (!s_cherry_serial) {
        ESP_LOGE(TAG, "CherryUSB CDC-ACM open timed out");
        return -1;
    }

    if (!s_cherry_serial->bulkin || s_cherry_serial->bulkin->wMaxPacketSize == 0 ||
        (EH_CHERRY_RX_LEN % s_cherry_serial->bulkin->wMaxPacketSize) != 0) {
        ESP_LOGE(TAG, "CDC bulk IN MPS is incompatible");
        usbh_serial_close(s_cherry_serial);
        s_cherry_serial = NULL;
        return -1;
    }

    s_cherry_rx_run = 1;
    {
        eh_host_port_task_create_cfg_t task_cfg = {
            .fn = cherry_rx_pump,
            .arg = NULL,
            .stack_bytes = CONFIG_ESP_HOSTED_HOST_USB_RX_TASK_STACK,
            .priority = CONFIG_ESP_HOSTED_HOST_USB_RX_TASK_PRIO,
            .name = "eh_usb_cherry_rx",
        };
        if (eh_host_port_task_create(&task_cfg, &s_cherry_rx_pump) != EH_HOST_PORT_OK) {
            ESP_LOGE(TAG, "failed to create CherryUSB RX pump");
            s_cherry_rx_run = 0;
            usbh_serial_close(s_cherry_serial);
            s_cherry_serial = NULL;
            return -1;
        }
    }

    /* Arm bulk IN before DTR so the coprocessor's INIT is not dropped. */
    int ret = usbh_serial_cdc_read_async(
        s_cherry_serial, s_cherry_rx_buf, EH_CHERRY_RX_LEN, cherry_rx_complete, NULL);
    if (ret < 0) {
        ESP_LOGE(TAG, "cdc read submit failed: %d", ret);
        s_cherry_closing = 1;
        cherry_stop_rx_pump();
        usbh_serial_close(s_cherry_serial);
        s_cherry_serial = NULL;
        s_cherry_closing = 0;
        return -1;
    }

    s_cdc_opened = 1;

    uint32_t flags = USBH_SERIAL_TIOCM_DTR;
    ret = usbh_serial_control(s_cherry_serial, USBH_SERIAL_CMD_TIOCMSET, &flags);
    if (ret < 0) {
        ESP_LOGW(TAG, "set DTR failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "Asserted DTR to coprocessor");
    }
    return 0;
}

static int cherry_stack_tx(const uint8_t *buf, size_t len)
{
    if (len > sizeof(s_cherry_tx_buf) || !s_cherry_serial || !s_cherry_tx_sem) {
        return -1;
    }

    memcpy(s_cherry_tx_buf, buf, len);
    eh_host_port_sem_try_wait(s_cherry_tx_sem);
    s_cherry_tx_nbytes = 0;

    int ret = usbh_serial_cdc_write_async(
        s_cherry_serial, s_cherry_tx_buf, (uint32_t)len, cherry_tx_complete, NULL);
    if (ret < 0) {
        ESP_LOGE(TAG, "cdc write submit failed: %d", ret);
        return -1;
    }

    if (eh_host_port_sem_wait_ms(s_cherry_tx_sem, EH_USB_TX_TIMEOUT_MS) != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "CherryUSB TX timeout");
        usbh_kill_urb(&s_cherry_serial->bulkout_urb);
        eh_host_port_sem_wait_ms(s_cherry_tx_sem, 100);
        return -1;
    }
    if (s_cherry_tx_nbytes < 0 || (size_t)s_cherry_tx_nbytes != len) {
        ESP_LOGE(TAG, "CherryUSB TX failed: %d", s_cherry_tx_nbytes);
        return -1;
    }
    return 0;
}

static void cherry_stack_close(void)
{
    s_cherry_closing = 1;
    s_cdc_opened = 0;
    cherry_stop_rx_pump();

    if (s_cherry_serial) {
        uint32_t flags = 0;
        usbh_serial_control(s_cherry_serial, USBH_SERIAL_CMD_TIOCMSET, &flags);
        usbh_serial_close(s_cherry_serial);
        s_cherry_serial = NULL;
    }

    if (s_cherry_host_up) {
        usbh_deinitialize(0);
        s_cherry_host_up = 0;
    }

    if (s_cherry_rx_sem) {
        eh_host_port_sem_destroy(s_cherry_rx_sem);
        s_cherry_rx_sem = NULL;
    }
    if (s_cherry_tx_sem) {
        eh_host_port_sem_destroy(s_cherry_tx_sem);
        s_cherry_tx_sem = NULL;
    }
    s_cherry_closing = 0;
}

static int cherry_stack_is_ready(void)
{
    return s_cdc_opened && s_cherry_serial ? 1 : 0;
}

const struct eh_usb_stack_ops eh_host_usb_stack_ops = {
    .name = "CherryUSB",
    .start = cherry_stack_start,
    .open = cherry_stack_open,
    .tx = cherry_stack_tx,
    .close = cherry_stack_close,
    .is_ready = cherry_stack_is_ready,
};

#endif /* EH_HOST_TRANSPORT_BUS_USB */
#endif /* ESP_PLATFORM */
