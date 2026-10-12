/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_log.h"

#include "eh_host_port_master_config.h"
#include "eh_host_transport_config.h"
#include "eh_check.h"

static const char *TAG = "eh_host_transport_config";

static struct eh_host_transport_config s_transport_config = { 0 };
static bool s_transport_config_set;
static bool s_transport_config_in_use;

bool eh_host_transport_is_config_valid(void) {
  return s_transport_config_set;
}

void eh_host_transport_config_set_in_use(bool in_use)
{
	s_transport_config_in_use = in_use;
}

/* While the bus runs on the config only an identical one is accepted, so a
 * recovery re-init can re-apply it. */
static eh_host_transport_err_t check_settable(const void *cur, const void *next, size_t len)
{
	if (s_transport_config_in_use && memcmp(cur, next, len) != 0) {
		ESP_LOGE(TAG, "transport in use: set_config only after esp_hosted_deinit()");
		return EH_HOST_TRANSPORT_RC_ERR_ALREADY_SET;
	}
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_transport_set_default_config(void)
{
#if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO
	EH_CHECK_OK(eh_host_sdio_set_config(NULL));
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD
	EH_CHECK_OK(eh_host_spi_hd_set_config(NULL));
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_SPI
	EH_CHECK_OK(eh_host_spi_set_config(NULL));
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_UART
	EH_CHECK_OK(eh_host_uart_set_config(NULL));
#else
	/* no MCU bus selected: mark shadow valid so init's seed-defaults is no-op */
	s_transport_config.transport_in_use = H_TRANSPORT_NONE;
	s_transport_config_set              = true;
#endif

	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_transport_get_config(struct eh_host_transport_config **config)
{
	if (!config) {
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	*config = &s_transport_config;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_transport_get_reset_config(eh_gpio_pin_t *pin_config)
{
	if (!pin_config) {
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}

	switch(s_transport_config.transport_in_use) {
	case H_TRANSPORT_SDIO:
		pin_config->port = s_transport_config.u.sdio.pin_reset.port;
		pin_config->pin  = s_transport_config.u.sdio.pin_reset.pin;
		break;
	case H_TRANSPORT_SPI_HD:
		pin_config->port = s_transport_config.u.spi_hd.pin_reset.port;
		pin_config->pin  = s_transport_config.u.spi_hd.pin_reset.pin;
		break;
	case H_TRANSPORT_SPI:
		pin_config->port = s_transport_config.u.spi.pin_reset.port;
		pin_config->pin  = s_transport_config.u.spi.pin_reset.pin;
		break;
	case H_TRANSPORT_UART:
		pin_config->port = s_transport_config.u.uart.pin_reset.port;
		pin_config->pin  = s_transport_config.u.uart.pin_reset.pin;
		break;
	case H_TRANSPORT_NONE:
	default:
		/* no transport yet — fall back to default reset pin */
		pin_config->port = H_GPIO_PORT_RESET;
		pin_config->pin  = H_GPIO_PIN_RESET;
		break;
	}

	return EH_HOST_TRANSPORT_RC_OK;
}

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO
/* rx_mode and block_mode select the data path at build time; rx_mode must also
 * match the co-processor. */
static eh_host_transport_err_t check_sdio_build(const struct eh_host_sdio_config *c)
{
	struct eh_host_sdio_config d = INIT_DEFAULT_HOST_SDIO_CONFIG();

	if (c->rx_mode != d.rx_mode || c->block_mode != d.block_mode) {
		ESP_LOGE(TAG, "sdio rx_mode/block_mode are fixed by the build (%u/%u)",
				(unsigned)d.rx_mode, (unsigned)d.block_mode);
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	return EH_HOST_TRANSPORT_RC_OK;
}

/* SDIO functions */
eh_host_transport_err_t eh_host_sdio_get_config(struct eh_host_sdio_config **config)
{
	if (!config) {
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	*config = &s_transport_config.u.sdio;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_sdio_set_config(struct eh_host_sdio_config *config)
{
	struct eh_host_sdio_config new_config = config ? *config : INIT_DEFAULT_HOST_SDIO_CONFIG();
	eh_host_transport_err_t rc;

	rc = check_sdio_build(&new_config);
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}
	rc = check_settable(&s_transport_config.u.sdio, &new_config, sizeof(new_config));
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}

	s_transport_config.u.sdio = new_config;
	s_transport_config_set = true;
	s_transport_config.transport_in_use = H_TRANSPORT_SDIO;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_sdio_iomux_set_config(struct eh_host_sdio_config *config)
{
	struct eh_host_sdio_config new_config = config ? *config : INIT_DEFAULT_HOST_SDIO_IOMUX_CONFIG();
	eh_host_transport_err_t rc;

	new_config.iomux_enable = true;
	rc = check_sdio_build(&new_config);
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}
	rc = check_settable(&s_transport_config.u.sdio, &new_config, sizeof(new_config));
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}

	s_transport_config.u.sdio = new_config;
	s_transport_config_set = true;
	s_transport_config.transport_in_use = H_TRANSPORT_SDIO;
	return EH_HOST_TRANSPORT_RC_OK;
}
#endif

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD
/* Command, address and dummy bits are fixed by the slave HD protocol. */
static eh_host_transport_err_t check_spi_hd_protocol(const struct eh_host_spi_hd_config *c)
{
	struct eh_host_spi_hd_config d = INIT_DEFAULT_HOST_SPI_HD_CONFIG();

	if (c->num_command_bits != d.num_command_bits || c->num_address_bits != d.num_address_bits ||
	    c->num_dummy_bits != d.num_dummy_bits) {
		ESP_LOGE(TAG, "spi_hd command/address/dummy bits are fixed by the protocol (%u/%u/%u)",
				(unsigned)d.num_command_bits, (unsigned)d.num_address_bits,
				(unsigned)d.num_dummy_bits);
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	if (c->num_data_lines != 1 && c->num_data_lines != 2 && c->num_data_lines != 4) {
		ESP_LOGE(TAG, "spi_hd data lines must be 1, 2 or 4");
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	return EH_HOST_TRANSPORT_RC_OK;
}

/* SPI Half Duplex functions */
eh_host_transport_err_t eh_host_spi_hd_get_config(struct eh_host_spi_hd_config **config)
{
	if (!config) {
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	*config = &s_transport_config.u.spi_hd;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_spi_hd_set_config(struct eh_host_spi_hd_config *config)
{
	struct eh_host_spi_hd_config new_config = config ? *config : INIT_DEFAULT_HOST_SPI_HD_CONFIG();
	eh_host_transport_err_t rc;

	rc = check_spi_hd_protocol(&new_config);
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}
	rc = check_settable(&s_transport_config.u.spi_hd, &new_config, sizeof(new_config));
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}

	s_transport_config.u.spi_hd = new_config;
	s_transport_config_set = true;
	s_transport_config.transport_in_use = H_TRANSPORT_SPI_HD;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_spi_hd_2lines_get_config(struct eh_host_spi_hd_config **config)
{
	if (!config) {
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	*config = &s_transport_config.u.spi_hd;
	s_transport_config.u.spi_hd.num_data_lines = 2;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_spi_hd_2lines_set_config(struct eh_host_spi_hd_config *config)
{
	struct eh_host_spi_hd_config new_config = config ? *config : INIT_DEFAULT_HOST_SPI_HD_CONFIG();
	eh_host_transport_err_t rc;

	new_config.num_data_lines = 2;
	rc = check_spi_hd_protocol(&new_config);
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}
	rc = check_settable(&s_transport_config.u.spi_hd, &new_config, sizeof(new_config));
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}

	s_transport_config.u.spi_hd = new_config;
	s_transport_config_set = true;
	s_transport_config.transport_in_use = H_TRANSPORT_SPI_HD;
	return EH_HOST_TRANSPORT_RC_OK;
}
#endif

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI
/* SPI Full Duplex functions */
eh_host_transport_err_t eh_host_spi_get_config(struct eh_host_spi_config **config)
{
	if (!config) {
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	*config = &s_transport_config.u.spi;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_spi_set_config(struct eh_host_spi_config *config)
{
	struct eh_host_spi_config new_config = config ? *config : INIT_DEFAULT_HOST_SPI_CONFIG();
	eh_host_transport_err_t rc;

	rc = check_settable(&s_transport_config.u.spi, &new_config, sizeof(new_config));
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}

	s_transport_config.u.spi = new_config;
	s_transport_config_set = true;
	s_transport_config.transport_in_use = H_TRANSPORT_SPI;
	return EH_HOST_TRANSPORT_RC_OK;
}
#endif

#if H_TRANSPORT_IN_USE == H_TRANSPORT_UART
/* parity 0/1/2 = none/even/odd, stop bits 0/1/2 = 1/1.5/2 */
static eh_host_transport_err_t check_uart_framing(const struct eh_host_uart_config *c)
{
	if (c->num_data_bits < 5 || c->num_data_bits > 8 || c->parity > 2 || c->stop_bits > 2 ||
	    !c->baud_rate) {
		ESP_LOGE(TAG, "uart framing out of range: data bits %u, parity %u, stop bits %u, baud %lu",
				(unsigned)c->num_data_bits, (unsigned)c->parity, (unsigned)c->stop_bits,
				(unsigned long)c->baud_rate);
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	return EH_HOST_TRANSPORT_RC_OK;
}

/* UART functions */
eh_host_transport_err_t eh_host_uart_get_config(struct eh_host_uart_config **config)
{
	if (!config) {
		return EH_HOST_TRANSPORT_RC_ERR_INVALID_ARG;
	}
	*config = &s_transport_config.u.uart;
	return EH_HOST_TRANSPORT_RC_OK;
}

eh_host_transport_err_t eh_host_uart_set_config(struct eh_host_uart_config *config)
{
	struct eh_host_uart_config new_config = config ? *config : INIT_DEFAULT_HOST_UART_CONFIG();
	eh_host_transport_err_t rc;

	rc = check_uart_framing(&new_config);
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}
	rc = check_settable(&s_transport_config.u.uart, &new_config, sizeof(new_config));
	if (rc != EH_HOST_TRANSPORT_RC_OK) {
		return rc;
	}

	s_transport_config.u.uart = new_config;
	s_transport_config_set = true;
	s_transport_config.transport_in_use = H_TRANSPORT_UART;
	return EH_HOST_TRANSPORT_RC_OK;
}
#endif
