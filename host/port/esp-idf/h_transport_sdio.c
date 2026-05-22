/* host/port/esp-idf/h_transport_sdio.c
 * ESP-IDF SDIO Transport port.
 * Only compiled when H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO.
 *
 * Adapts the existing hosted_sdio_* implementations from
 * host/port/esp/freertos/src/port_esp_hosted_host_sdio.c
 * to the h_transport_contract_t vtable. */

#include "h_port_contract.h"
#include "h_port_config.h"

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO

#include <esp_err.h>
#include "freertos/FreeRTOS.h"  /* pdMS_TO_TICKS */

/* ──  Existing implementations (from host/port/esp/freertos/src/) ── */
extern void *hosted_sdio_init(void);
extern int   hosted_sdio_deinit(void *ctx);
extern int   hosted_sdio_card_init(void *ctx, bool show_config);
extern int   hosted_sdio_read_reg(void *ctx, uint32_t reg, uint8_t *data,
                                  uint16_t size, bool lock_required);
extern int   hosted_sdio_write_reg(void *ctx, uint32_t reg, uint8_t *data,
                                   uint16_t size, bool lock_required);
extern int   hosted_sdio_read_block(void *ctx, uint32_t reg, uint8_t *data,
                                    uint16_t size, bool lock_required);
extern int   hosted_sdio_write_block(void *ctx, uint32_t reg, uint8_t *data,
                                     uint16_t size, bool lock_required);
extern int   hosted_sdio_wait_slave_intr(void *ctx, uint32_t ticks_to_wait);
extern int   ensure_slave_bus_ready(void *bus_handle);
extern int   esp_hosted_tx(uint8_t iface_type, uint8_t iface_num,
                           uint8_t *payload_buf, uint16_t payload_len,
                           uint8_t buff_zerocopy, uint8_t *buffer_to_free,
                           void (*free_buf_func)(void *ptr), uint8_t flags);

/* GPIO — from host/port/esp/freertos/src/port_esp_hosted_host_os.c */
extern int hosted_config_gpio(void *gpio_port, uint32_t gpio_num,
                              uint32_t mode);
extern int hosted_setup_gpio_interrupt(void *gpio_port, uint32_t gpio_num,
                                       uint32_t intr_type,
                                       void (*fn)(void *), void *arg);
extern int hosted_teardown_gpio_interrupt(void *gpio_port, uint32_t gpio_num);
extern int hosted_read_gpio(void *gpio_port, uint32_t gpio_num);
extern int hosted_write_gpio(void *gpio_port, uint32_t gpio_num,
                             uint32_t value);

/* ──  Adapters ── */

static int h_sdio_init_adapter(void **out_handle)
{
    *out_handle = hosted_sdio_init();
    return (*out_handle != NULL) ? H_OK : H_FAIL;
}

static int h_sdio_deinit_adapter(void *handle)
{
    int ret = hosted_sdio_deinit(handle);
    return (ret == 0) ? H_OK : H_FAIL;
}

/* SDIO card init — pass-through */
static int h_sdio_card_init_adapter(void *handle, bool show_config)
{
    int ret = hosted_sdio_card_init(handle, show_config);
    return (ret == 0) ? H_OK : H_FAIL;
}

static int h_sdio_read_reg_adapter(void *handle, uint32_t reg, uint8_t *data,
                                   uint16_t size, bool lock)
{
    int ret = hosted_sdio_read_reg(handle, reg, data, size, lock);
    return (ret == 0) ? H_OK : H_FAIL;
}

static int h_sdio_write_reg_adapter(void *handle, uint32_t reg, uint8_t *data,
                                    uint16_t size, bool lock)
{
    int ret = hosted_sdio_write_reg(handle, reg, data, size, lock);
    return (ret == 0) ? H_OK : H_FAIL;
}

static int h_sdio_read_block_adapter(void *handle, uint32_t reg,
                                     uint8_t *data, uint16_t size, bool lock)
{
    int ret = hosted_sdio_read_block(handle, reg, data, size, lock);
    return (ret == 0) ? H_OK : H_FAIL;
}

static int h_sdio_write_block_adapter(void *handle, uint32_t reg,
                                      uint8_t *data, uint16_t size, bool lock)
{
    int ret = hosted_sdio_write_block(handle, reg, data, size, lock);
    return (ret == 0) ? H_OK : H_FAIL;
}

/* hosted_sdio_wait_slave_intr takes FreeRTOS ticks; contract says timeout_ms.
 * The caller is responsible for converting ms to ticks before calling. */
static int h_sdio_wait_intr_adapter(void *handle, uint32_t timeout_ms)
{
    int ret = hosted_sdio_wait_slave_intr(handle, pdMS_TO_TICKS(timeout_ms));
    return (ret == 0) ? H_OK : H_ERR_TIMEOUT;
}

/* GPIO */
static int h_gpio_config_adapter(uint32_t pin, uint32_t mode)
{
    hosted_config_gpio(NULL, pin, mode);
    return H_OK;
}

static int h_gpio_set_intr_adapter(uint32_t pin, uint32_t intr_type,
                                   void (*isr)(void*), void *arg)
{
    hosted_setup_gpio_interrupt(NULL, pin, intr_type, isr, arg);
    return H_OK;
}

static int h_gpio_clear_intr_adapter(uint32_t pin)
{
    hosted_teardown_gpio_interrupt(NULL, pin);
    return H_OK;
}

static int h_gpio_read_adapter(uint32_t pin)
{
    return hosted_read_gpio(NULL, pin);
}

static int h_gpio_write_adapter(uint32_t pin, uint32_t value)
{
    hosted_write_gpio(NULL, pin, value);
    return H_OK;
}

/* ──  Global Transport Contract Instance (SDIO) ── */

const h_transport_contract_t g_h_transport = {
    .init           = h_sdio_init_adapter,
    .deinit         = h_sdio_deinit_adapter,
    .bus_ready      = ensure_slave_bus_ready,
    .transmit       = esp_hosted_tx,

    /* SPI — not used in SDIO transport */
    .spi_transfer   = NULL,

    /* SDIO */
    .sdio_card_init = h_sdio_card_init_adapter,
    .sdio_read_reg  = h_sdio_read_reg_adapter,
    .sdio_write_reg = h_sdio_write_reg_adapter,
    .sdio_read_block = h_sdio_read_block_adapter,
    .sdio_write_block = h_sdio_write_block_adapter,
    .sdio_wait_intr = h_sdio_wait_intr_adapter,

    /* UART — not used in SDIO transport */
    .uart_read      = NULL,
    .uart_write     = NULL,
    .uart_flush     = NULL,

    /* GPIO */
    .gpio_config    = h_gpio_config_adapter,
    .gpio_set_intr  = h_gpio_set_intr_adapter,
    .gpio_clear_intr = h_gpio_clear_intr_adapter,
    .gpio_read      = h_gpio_read_adapter,
    .gpio_write     = h_gpio_write_adapter,

    /* netif — NULL for Phase 1 */
    .netif_create   = NULL,
    .netif_destroy  = NULL,
};

#endif /* H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO */
