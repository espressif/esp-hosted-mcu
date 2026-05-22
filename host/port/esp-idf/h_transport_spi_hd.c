/* host/port/esp-idf/h_transport_spi_hd.c
 * ESP-IDF SPI-HD Transport port.
 * Only compiled when H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD.
 *
 * Adapts the existing hosted_spi_hd_* implementations from
 * host/port/esp/freertos/src/port_esp_hosted_host_spi_hd.c
 * to the h_transport_contract_t vtable.
 */

#include "h_port_contract.h"
#include "h_port_config.h"

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD

#include <esp_err.h>
#include <driver/gpio.h>

/* ── Existing implementations (from host/port/esp/freertos/src/) ── */
extern void *hosted_spi_hd_init(void);
extern int   hosted_spi_hd_deinit(void *ctx);

extern int hosted_spi_hd_read_reg(uint32_t reg, uint32_t *data,
                                  int poll, bool lock_required);
extern int hosted_spi_hd_write_reg(uint32_t reg, uint32_t *data,
                                   bool lock_required);
extern int hosted_spi_hd_read_dma(uint8_t *data, uint16_t size,
                                  bool lock_required);
extern int hosted_spi_hd_write_dma(uint8_t *data, uint16_t size,
                                   bool lock_required);
extern int hosted_spi_hd_send_cmd9(void);
extern int ensure_slave_bus_ready(void *bus_handle);
extern int esp_hosted_tx(uint8_t iface_type, uint8_t iface_num,
                         uint8_t *payload_buf, uint16_t payload_len,
                         uint8_t buff_zerocopy, uint8_t *buffer_to_free,
                         void (*free_buf_func)(void *ptr), uint8_t flags);

extern int hosted_config_gpio(void *gpio_port, uint32_t gpio_num,
                              uint32_t mode);
extern int hosted_setup_gpio_interrupt(void *gpio_port, uint32_t gpio_num,
                                       uint32_t intr_type,
                                       void (*fn)(void *), void *arg);
extern int hosted_teardown_gpio_interrupt(void *gpio_port, uint32_t gpio_num);
extern int hosted_read_gpio(void *gpio_port, uint32_t gpio_num);
extern int hosted_write_gpio(void *gpio_port, uint32_t gpio_num,
                             uint32_t value);

/* ── Adapters ── */

static int h_spi_hd_init_adapter(void **out_handle)
{
    *out_handle = hosted_spi_hd_init();
    return (*out_handle != NULL) ? H_OK : H_FAIL;
}

static int h_spi_hd_deinit_adapter(void *handle)
{
    int ret = hosted_spi_hd_deinit(handle);
    return (ret == 0) ? H_OK : H_FAIL;
}

/* SPI-HD specific operations — hosted_* functions currently use global ctx,
 * so the handle parameter is passed but not used internally.
 * This is a known limitation that will be addressed when multi-device
 * support is needed. */
static int h_spi_hd_read_reg_adapter(void *handle, uint32_t reg,
                                      uint32_t *data, int poll, bool lock)
{
    (void)handle;
    return hosted_spi_hd_read_reg(reg, data, poll, lock);
}

static int h_spi_hd_write_reg_adapter(void *handle, uint32_t reg,
                                       uint32_t *data, bool lock)
{
    (void)handle;
    return hosted_spi_hd_write_reg(reg, data, lock);
}

static int h_spi_hd_read_dma_adapter(void *handle, uint8_t *data,
                                      uint16_t size, bool lock)
{
    (void)handle;
    return hosted_spi_hd_read_dma(data, size, lock);
}

static int h_spi_hd_write_dma_adapter(void *handle, uint8_t *data,
                                       uint16_t size, bool lock)
{
    (void)handle;
    return hosted_spi_hd_write_dma(data, size, lock);
}

static int h_spi_hd_send_cmd9_adapter(void *handle)
{
    (void)handle;
    return hosted_spi_hd_send_cmd9();
}

/* GPIO — old functions take (gpio_port, pin, ...), contract takes (pin, ...) */
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

/* ── Global Transport Contract Instance (SPI-HD) ── */

const h_transport_contract_t g_h_transport = {
    .init           = h_spi_hd_init_adapter,
    .deinit         = h_spi_hd_deinit_adapter,
    .bus_ready      = ensure_slave_bus_ready,
    .transmit       = esp_hosted_tx,

    /* SPI Full-Duplex — not used in SPI-HD transport */
    .spi_transfer   = NULL,

    /* SPI-HD */
    .spi_hd_read_reg  = h_spi_hd_read_reg_adapter,
    .spi_hd_write_reg = h_spi_hd_write_reg_adapter,
    .spi_hd_read_dma  = h_spi_hd_read_dma_adapter,
    .spi_hd_write_dma = h_spi_hd_write_dma_adapter,
    .spi_hd_send_cmd9 = h_spi_hd_send_cmd9_adapter,

    /* SDIO — not used in SPI-HD transport */
    .sdio_card_init   = NULL,
    .sdio_read_reg    = NULL,
    .sdio_write_reg   = NULL,
    .sdio_read_block  = NULL,
    .sdio_write_block = NULL,
    .sdio_wait_intr   = NULL,

    /* UART — not used in SPI-HD transport */
    .uart_read        = NULL,
    .uart_write       = NULL,
    .uart_flush       = NULL,

    /* GPIO */
    .gpio_config      = h_gpio_config_adapter,
    .gpio_set_intr    = h_gpio_set_intr_adapter,
    .gpio_clear_intr  = h_gpio_clear_intr_adapter,
    .gpio_read        = h_gpio_read_adapter,
    .gpio_write       = h_gpio_write_adapter,

    /* netif — NULL for Phase 1 (ESP-IDF uses static netif creation) */
    .netif_create     = NULL,
    .netif_destroy    = NULL,
};

#endif /* H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD */
