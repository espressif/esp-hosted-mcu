/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ESP_HOSTED_TRANSPORT_CONFIG_H__
#define __ESP_HOSTED_TRANSPORT_CONFIG_H__

#include <stdbool.h>

#include "port_esp_hosted_host_config.h"
#include "esp_err.h"

typedef enum {
	ESP_TRANSPORT_OK = ESP_OK,
	ESP_TRANSPORT_ERR_INVALID_ARG = ESP_ERR_INVALID_ARG,
	ESP_TRANSPORT_ERR_ALREADY_SET = ESP_ERR_NOT_ALLOWED,
	ESP_TRANSPORT_ERR_INVALID_STATE = ESP_ERR_INVALID_STATE,
} esp_hosted_transport_err_t;

/* GPIO pin configuration structure */
typedef struct {
	void *port;
	int pin;
} gpio_pin_t;

/* New Configuration Structures */
struct esp_hosted_sdio_config {
	uint32_t clock_freq_khz;
	uint8_t bus_width;
	uint8_t slot;
	gpio_pin_t pin_clk;
	gpio_pin_t pin_cmd;
	gpio_pin_t pin_d0;
	gpio_pin_t pin_d1;
	gpio_pin_t pin_d2;
	gpio_pin_t pin_d3;
	gpio_pin_t pin_reset;
	uint8_t rx_mode;
	bool block_mode;
	bool iomux_enable;
	uint16_t tx_queue_size;
	uint16_t rx_queue_size;
};

struct esp_hosted_spi_hd_config {
	/* Number of lines used */
	uint8_t num_data_lines;

	/* SPI HD pins */
	gpio_pin_t pin_cs;
	gpio_pin_t pin_clk;
	gpio_pin_t pin_data_ready;
	gpio_pin_t pin_d0;
	gpio_pin_t pin_d1;
	gpio_pin_t pin_d2;
	gpio_pin_t pin_d3;
	gpio_pin_t pin_reset;

	/* SPI HD configuration */
	uint32_t clk_mhz;
	uint8_t mode;
	uint16_t tx_queue_size;
	uint16_t rx_queue_size;
	bool checksum_enable;
	uint8_t num_command_bits;
	uint8_t num_address_bits;
	uint8_t num_dummy_bits;
};

struct esp_hosted_spi_config {
	/* SPI Full Duplex pins */
	gpio_pin_t pin_mosi;
	gpio_pin_t pin_miso;
	gpio_pin_t pin_sclk;
	gpio_pin_t pin_cs;
	gpio_pin_t pin_handshake;
	gpio_pin_t pin_data_ready;
	gpio_pin_t pin_reset;

	/* SPI Full Duplex configuration */
	uint16_t tx_queue_size;
	uint16_t rx_queue_size;
	uint8_t mode;
	uint32_t clk_mhz;
};

struct esp_hosted_uart_config {
	/* UART bus number */
	uint8_t port;

	/* UART pins */
	gpio_pin_t pin_tx;
	gpio_pin_t pin_rx;
	gpio_pin_t pin_reset;

	/* UART configuration */
	uint8_t num_data_bits;
	uint8_t parity;
	uint8_t stop_bits;
	uint8_t flow_ctrl;
	uint8_t clk_src;
	bool checksum_enable;
	uint32_t baud_rate;
	uint16_t tx_queue_size;
	uint16_t rx_queue_size;
};

struct esp_hosted_transport_config {
	uint8_t transport_in_use;
	union {
		struct esp_hosted_sdio_config sdio;
		struct esp_hosted_spi_hd_config spi_hd;
		struct esp_hosted_spi_config spi;
		struct esp_hosted_uart_config uart;
	} u;
};

/***
 * A port dependent header file is expected to provide the actual H_xxx definitions
 ***/
#define INIT_DEFAULT_HOST_SDIO_CONFIG() \
    (struct esp_hosted_sdio_config) { \
        .clock_freq_khz = H_SDIO_CLOCK_FREQ_KHZ, \
        .bus_width = H_SDIO_BUS_WIDTH, \
        .slot = H_SDMMC_HOST_SLOT, \
        .pin_clk = {.port = H_SDIO_PORT_CLK, .pin = H_SDIO_PIN_CLK}, \
        .pin_cmd = {.port = H_SDIO_PORT_CMD, .pin = H_SDIO_PIN_CMD}, \
        .pin_d0  = {.port = H_SDIO_PORT_D0,  .pin = H_SDIO_PIN_D0},  \
        .pin_d1  = {.port = H_SDIO_PORT_D1,  .pin = H_SDIO_PIN_D1},  \
        .pin_d2  = {.port = H_SDIO_PORT_D2,  .pin = H_SDIO_PIN_D2},  \
        .pin_d3  = {.port = H_SDIO_PORT_D3,  .pin = H_SDIO_PIN_D3},  \
        .pin_reset = {.port = H_GPIO_PORT_RESET, .pin = H_GPIO_PIN_RESET }, \
        .rx_mode = H_SDIO_HOST_RX_MODE, \
        .block_mode = H_SDIO_TX_BLOCK_ONLY_XFER && H_SDIO_RX_BLOCK_ONLY_XFER, \
        .iomux_enable = false, \
        .tx_queue_size = H_SDIO_TX_Q, \
        .rx_queue_size = H_SDIO_RX_Q, \
    }

#define INIT_DEFAULT_HOST_SDIO_IOMUX_CONFIG() \
    (struct esp_hosted_sdio_config) { \
        .clock_freq_khz = H_SDIO_CLOCK_FREQ_KHZ, \
        .bus_width = H_SDIO_BUS_WIDTH, \
        .slot = H_SDMMC_HOST_SLOT, \
        .rx_mode = H_SDIO_HOST_RX_MODE, \
        .block_mode = H_SDIO_TX_BLOCK_ONLY_XFER && H_SDIO_RX_BLOCK_ONLY_XFER, \
        .iomux_enable = true, \
    }

#define INIT_DEFAULT_HOST_SPI_HD_CONFIG() \
    (struct esp_hosted_spi_hd_config) { \
        .num_data_lines = H_SPI_HD_HOST_NUM_DATA_LINES, \
        .pin_cs = {.port = H_SPI_HD_PORT_CS, .pin = H_SPI_HD_PIN_CS}, \
        .pin_clk = {.port = H_SPI_HD_PORT_CLK, .pin = H_SPI_HD_PIN_CLK}, \
        .pin_data_ready = {.port = H_SPI_HD_PORT_DATA_READY, .pin = H_SPI_HD_PIN_DATA_READY}, \
        .pin_d0 = {.port = H_SPI_HD_PORT_D0, .pin = H_SPI_HD_PIN_D0}, \
        .pin_d1 = {.port = H_SPI_HD_PORT_D1, .pin = H_SPI_HD_PIN_D1}, \
        .pin_d2 = {.port = H_SPI_HD_PORT_D2, .pin = H_SPI_HD_PIN_D2}, \
        .pin_d3 = {.port = H_SPI_HD_PORT_D3, .pin = H_SPI_HD_PIN_D3}, \
        .pin_reset = {.port = H_GPIO_PORT_RESET, .pin = H_GPIO_PIN_RESET }, \
        .clk_mhz = H_SPI_HD_CLK_MHZ, \
        .mode = H_SPI_HD_MODE, \
        .tx_queue_size = H_SPI_HD_TX_QUEUE_SIZE, \
        .rx_queue_size = H_SPI_HD_RX_QUEUE_SIZE, \
        .checksum_enable = H_SPI_HD_CHECKSUM, \
        .num_command_bits = H_SPI_HD_NUM_COMMAND_BITS, \
        .num_address_bits = H_SPI_HD_NUM_ADDRESS_BITS, \
        .num_dummy_bits = H_SPI_HD_NUM_DUMMY_BITS, \
    }

#define INIT_DEFAULT_HOST_SPI_CONFIG() \
    (struct esp_hosted_spi_config) { \
        .pin_mosi = {.port = H_GPIO_MOSI_Port, .pin = H_GPIO_MOSI_Pin}, \
        .pin_miso = {.port = H_GPIO_MISO_Port, .pin = H_GPIO_MISO_Pin}, \
        .pin_sclk = {.port = H_GPIO_SCLK_Port, .pin = H_GPIO_SCLK_Pin}, \
        .pin_cs   = {.port = H_GPIO_CS_Port,   .pin = H_GPIO_CS_Pin},   \
        .pin_handshake = {.port = H_GPIO_HANDSHAKE_Port, .pin = H_GPIO_HANDSHAKE_Pin}, \
        .pin_data_ready = {.port = H_GPIO_DATA_READY_Port, .pin = H_GPIO_DATA_READY_Pin}, \
        .pin_reset = {.port = H_GPIO_PORT_RESET, .pin = H_GPIO_PIN_RESET }, \
        .tx_queue_size = H_SPI_TX_Q, \
        .rx_queue_size = H_SPI_RX_Q, \
        .mode = H_SPI_MODE, \
        .clk_mhz = H_SPI_FD_CLK_MHZ, \
    }

#define INIT_DEFAULT_HOST_UART_CONFIG() \
    (struct esp_hosted_uart_config) { \
        .port = H_UART_PORT, \
        .pin_tx = {.port = H_UART_PORT_TX, .pin = H_UART_PIN_TX}, \
        .pin_rx = {.port = H_UART_PORT_RX, .pin = H_UART_PIN_RX}, \
        .pin_reset = {.port = H_GPIO_PORT_RESET, .pin = H_GPIO_PIN_RESET }, \
        .num_data_bits = H_UART_NUM_DATA_BITS, \
        .parity = H_UART_PARITY, \
        .stop_bits = H_UART_STOP_BITS, \
        .flow_ctrl = H_UART_FLOWCTRL, \
        .clk_src = H_UART_CLK_SRC, \
        .checksum_enable = H_UART_CHECKSUM, \
        .baud_rate = H_UART_BAUD_RATE, \
        .tx_queue_size = H_UART_TX_QUEUE_SIZE, \
        .rx_queue_size = H_UART_RX_QUEUE_SIZE, \
    }

/***
 * Generic Transport APIs
 ***/
esp_err_t esp_hosted_set_default_config(void);
bool esp_hosted_is_config_valid(void);

/* Configuration get/set functions */
esp_hosted_transport_err_t esp_hosted_transport_set_default_config(void);
esp_hosted_transport_err_t esp_hosted_transport_get_config(struct esp_hosted_transport_config **config);
esp_hosted_transport_err_t esp_hosted_transport_get_reset_config(gpio_pin_t *pin_config);

bool esp_hosted_transport_is_config_valid(void);

/***
 * Transport dependent APIs.
 * Can only be used with the configured host transport
 ***/
/* SDIO functions */
esp_hosted_transport_err_t esp_hosted_sdio_get_config(struct esp_hosted_sdio_config **config);
esp_hosted_transport_err_t esp_hosted_sdio_set_config(struct esp_hosted_sdio_config *config) __attribute__((warn_unused_result));

esp_hosted_transport_err_t esp_hosted_sdio_iomux_set_config(struct esp_hosted_sdio_config *config) __attribute__((warn_unused_result));

/* SPI Half Duplex functions */
esp_hosted_transport_err_t esp_hosted_spi_hd_get_config(struct esp_hosted_spi_hd_config **config);
esp_hosted_transport_err_t esp_hosted_spi_hd_set_config(struct esp_hosted_spi_hd_config *config) __attribute__((warn_unused_result));

esp_hosted_transport_err_t esp_hosted_spi_hd_2lines_get_config(struct esp_hosted_spi_hd_config **config);
esp_hosted_transport_err_t esp_hosted_spi_hd_2lines_set_config(struct esp_hosted_spi_hd_config *config) __attribute__((warn_unused_result));

/* SPI Full Duplex functions */
esp_hosted_transport_err_t esp_hosted_spi_get_config(struct esp_hosted_spi_config **config);
esp_hosted_transport_err_t esp_hosted_spi_set_config(struct esp_hosted_spi_config *config) __attribute__((warn_unused_result));

/* UART functions */
esp_hosted_transport_err_t esp_hosted_uart_get_config(struct esp_hosted_uart_config **config);
esp_hosted_transport_err_t esp_hosted_uart_set_config(struct esp_hosted_uart_config *config) __attribute__((warn_unused_result));

#endif /* __ESP_HOSTED_TRANSPORT_CONFIG_H__ */
