/* Stub for Linux mock build -- minimal feature macros */
#ifndef PORT_ESP_HOSTED_HOST_CONFIG_H
#define PORT_ESP_HOSTED_HOST_CONFIG_H

#include <stdbool.h>
#include "sdkconfig.h"

#define H_TRANSPORT_NONE 0
#define H_TRANSPORT_SDIO 1
#define H_TRANSPORT_SPI_HD 2
#define H_TRANSPORT_SPI 3
#define H_TRANSPORT_UART 4

/* Note: H_TRANSPORT_IN_USE is defined by h_port_config.h in mock build */

#define H_GPIO_LOW  0
#define H_GPIO_HIGH 1

#define H_ENABLE 1
#define H_DISABLE 0

enum {
    H_GPIO_INTR_DISABLE = 0,
    H_GPIO_INTR_POSEDGE = 1,
    H_GPIO_INTR_NEGEDGE = 2,
    H_GPIO_INTR_ANYEDGE = 3,
    H_GPIO_INTR_LOW_LEVEL = 4,
    H_GPIO_INTR_HIGH_LEVEL = 5,
    H_GPIO_INTR_MAX,
};

#define H_USE_MEMPOOL 1

/* Fallback defaults for files that include this stub before h_config.h.
 * h_config.h defines these as 5; the stub overrides to 2 for mock builds
 * where smaller queues are sufficient and avoid large static allocations. */
#ifndef H_MAX_SYNC_RPC_REQUESTS
#define H_MAX_SYNC_RPC_REQUESTS  2
#endif
#ifndef H_MAX_ASYNC_RPC_REQUESTS
#define H_MAX_ASYNC_RPC_REQUESTS 2
#endif

#define H_HANDSHAKE_ACTIVE_HIGH 1
#define H_DATAREADY_ACTIVE_HIGH 1

#define H_HS_VAL_ACTIVE   H_GPIO_HIGH
#define H_HS_VAL_INACTIVE H_GPIO_LOW
#define H_HS_INTR_EDGE    H_GPIO_INTR_POSEDGE

#define H_DR_VAL_ACTIVE   H_GPIO_HIGH
#define H_DR_VAL_INACTIVE H_GPIO_LOW
#define H_DR_INTR_EDGE    H_GPIO_INTR_POSEDGE

#define H_GPIO_HANDSHAKE_Port  NULL
#define H_GPIO_HANDSHAKE_Pin   0
#define H_GPIO_DATA_READY_Port NULL
#define H_GPIO_DATA_READY_Pin  0

#define H_GPIO_MOSI_Port NULL
#define H_GPIO_MOSI_Pin  0
#define H_GPIO_MISO_Port NULL
#define H_GPIO_MISO_Pin  0
#define H_GPIO_SCLK_Port NULL
#define H_GPIO_SCLK_Pin  0
#define H_GPIO_CS_Port   NULL
#define H_GPIO_CS_Pin    0

#define H_SPI_TX_Q  5
#define H_SPI_RX_Q  5
#define H_SPI_MODE  0
#define H_SPI_FD_CLK_MHZ 10

#define H_TRANSPORT_QUEUE_SIZE 5

#define H_SPI_HD_HOST_INTERFACE 0

#define H_UART_HOST_TRANSPORT 0

#define H_GPIO_PIN_RESET  0
#define H_GPIO_PORT_RESET NULL

#define H_RESET_ACTIVE_HIGH 1
#define H_RESET_VAL_ACTIVE   H_GPIO_HIGH
#define H_RESET_VAL_INACTIVE H_GPIO_LOW

#define H_TRANSPORT_RESTART_ON_FAILURE 0
#define H_SLAVE_RESET_ON_EVERY_HOST_BOOTUP 1
#define H_SLAVE_RESET_ONLY_IF_NECESSARY 0

#define H_HOST_RESTART_NO_COMMUNICATION_WITH_SLAVE 0
#define H_HOST_RESTART_NO_COMMUNICATION_WITH_SLAVE_TIMEOUT_MS -1

#define TIMEOUT_PSERIAL_RESP 30

#define PRE_FORMAT_NEWLINE_CHAR  ""
#define POST_FORMAT_NEWLINE_CHAR "\n"

#define USE_STD_C_LIB_MALLOC 0

#define H_WIFI_TX_DATA_THROTTLE_LOW_THRESHOLD  0
#define H_WIFI_TX_DATA_THROTTLE_HIGH_THRESHOLD 0

#define H_TEST_RAW_TP 0
#define H_TEST_RAW_TP_DIR 0

#define H_MEM_MONITOR 0

#define H_HOST_PS_ALLOWED 0
#define H_HOST_WAKEUP_GPIO -1
#define H_HOST_WAKEUP_GPIO_LEVEL 1

#define H_HOST_SDIO_RESET_DELAY_MS 1500

#define H_PEER_DATA_TRANSFER 0
#define H_NETWORK_SPLIT_ENABLED 0

#define H_HOST_USES_STATIC_NETIF 0

#define H_GPIO_EXPANDER_SUPPORT 0
#define H_EXT_COEX_SUPPORT 0
#define H_EXT_COEX_ADVANCE_SUPPORT 0

#define H_ESP_HOSTED_HOST 1

#define MAX_TRANSPORT_BUFFER_SIZE 1600

#define unlikely(x) (x)

/* For non-ESP host, pick a default slave target */
#define H_SLAVE_TARGET_ESP32C6 1

#define ESP_PLATFORM 0

/* minimal forward declarations */
typedef int esp_err_t;

/* esp_hosted_set_default_config and esp_hosted_is_config_valid are declared
 * in esp_hosted_transport_config.h; definitions provided by
 * tests/stubs/rpc_wifi_stubs.c to avoid static inline / non-static
 * declaration mismatch. */

#endif
