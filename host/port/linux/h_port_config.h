/* host/port/linux/h_port_config.h */
#ifndef H_PORT_CONFIG_LINUX_H
#define H_PORT_CONFIG_LINUX_H

#define H_TRANSPORT_IN_USE  H_TRANSPORT_SPI  /* Mock SPI transport */

#define H_PORT_NAME         "linux-mock"
#define H_PORT_VERSION      "0.1.0"
#define H_PORT_RTOS         "posix"
#define H_PORT_RTOS_VER     "glibc"
#define H_PORT_CHIP         "x86_64"
#define H_PORT_BUILD_DATE   __DATE__

/* No static netif on Linux mock */
#define H_HOST_USES_STATIC_NETIF 0

#define H_DEFAULT_TASK_STACK  (256 * 1024)
#define H_DEFAULT_TASK_PRIO   0

/* Phase 2 feature flags */
#define H_FEATURE_BLUETOOTH  0
#define H_FEATURE_OTA        0
#define H_FEATURE_NETSPLIT   0

/* Transport config */
#define H_TEST_RAW_TP_DIR  0
#define H_WIFI_TX_DATA_THROTTLE_LOW_THRESHOLD  0
#define H_WIFI_TX_DATA_THROTTLE_HIGH_THRESHOLD 0

#endif
