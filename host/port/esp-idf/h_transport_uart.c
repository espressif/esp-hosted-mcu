/* host/port/esp-idf/h_transport_uart.c
 * ESP-IDF UART Transport port.
 * Only compiled when H_TRANSPORT_IN_USE == H_TRANSPORT_UART.
 *
 * Adapts the existing hosted_uart_* implementations from
 * host/port/esp/freertos/src/port_esp_hosted_host_uart.c
 * to the h_transport_contract_t vtable. */

#include "h_port_contract.h"
#include "h_port_config.h"

#if H_TRANSPORT_IN_USE == H_TRANSPORT_UART

#include <esp_err.h>

/* ──  Existing implementations (from host/port/esp/freertos/src/) ── */
extern void     *hosted_uart_init(void);
extern esp_err_t hosted_uart_deinit(void *ctx);
extern int       hosted_uart_read(void *ctx, uint8_t *data, uint16_t size);
extern int       hosted_uart_write(void *ctx, uint8_t *data, uint16_t size);
extern int       hosted_uart_flush_input(void *ctx);
extern int       ensure_slave_bus_ready(void *bus_handle);
extern int       esp_hosted_tx(uint8_t iface_type, uint8_t iface_num,
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

static int h_uart_init_adapter(void **out_handle)
{
    *out_handle = hosted_uart_init();
    return (*out_handle != NULL) ? H_OK : H_FAIL;
}

static int h_uart_deinit_adapter(void *handle)
{
    esp_err_t ret = hosted_uart_deinit(handle);
    return (ret == ESP_OK) ? H_OK : H_FAIL;
}

/* hosted_uart_read returns the number of bytes read (int); negative on error.
 * The contract also returns int — pass-through with error translation. */
static int h_uart_read_adapter(void *handle, uint8_t *data, uint16_t size)
{
    int ret = hosted_uart_read(handle, data, size);
    return (ret >= 0) ? ret : H_FAIL;
}

/* hosted_uart_write returns the number of bytes written (int); negative on error */
static int h_uart_write_adapter(void *handle, uint8_t *data, uint16_t size)
{
    int ret = hosted_uart_write(handle, data, size);
    return (ret >= 0) ? ret : H_FAIL;
}

static int h_uart_flush_adapter(void *handle)
{
    int ret = hosted_uart_flush_input(handle);
    return (ret == ESP_OK) ? H_OK : H_FAIL;
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

/* ──  Global Transport Contract Instance (UART) ── */

const h_transport_contract_t g_h_transport = {
    .init           = h_uart_init_adapter,
    .deinit         = h_uart_deinit_adapter,
    .bus_ready      = ensure_slave_bus_ready,
    .transmit       = esp_hosted_tx,

    /* SPI — not used in UART transport */
    .spi_transfer   = NULL,

    /* SDIO — not used in UART transport */
    .sdio_card_init = NULL,
    .sdio_read_reg  = NULL,
    .sdio_write_reg = NULL,
    .sdio_read_block = NULL,
    .sdio_write_block = NULL,
    .sdio_wait_intr = NULL,

    /* UART */
    .uart_read      = h_uart_read_adapter,
    .uart_write     = h_uart_write_adapter,
    .uart_flush     = h_uart_flush_adapter,

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

#endif /* H_TRANSPORT_IN_USE == H_TRANSPORT_UART */
