/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* SDIO bus. Part of eh_cp_master_config.h: include that header, not this one. */

#ifndef EH_CP_CFG_BUS_SDIO_H
#define EH_CP_CFG_BUS_SDIO_H

#ifndef EH_CP_MASTER_CONFIG_H
#error "include eh_cp_master_config.h, not eh_cp_cfg_bus_sdio.h"
#endif

/* SDIO. */

#if EH_CP_TRANSPORT_SDIO
#  define EH_CP_SDIO_PIN_CMD            CONFIG_EH_TRANSPORT_CP_SDIO_PIN_CMD
#  define EH_CP_SDIO_PIN_CLK            CONFIG_EH_TRANSPORT_CP_SDIO_PIN_CLK
#  define EH_CP_SDIO_PIN_D0             CONFIG_EH_TRANSPORT_CP_SDIO_PIN_D0
#  define EH_CP_SDIO_PIN_D1             CONFIG_EH_TRANSPORT_CP_SDIO_PIN_D1
#  define EH_CP_SDIO_PIN_D2             CONFIG_EH_TRANSPORT_CP_SDIO_PIN_D2
#  define EH_CP_SDIO_PIN_D3             CONFIG_EH_TRANSPORT_CP_SDIO_PIN_D3
#  define EH_CP_SDIO_PIN_RESET          CONFIG_EH_TRANSPORT_CP_SDIO_GPIO_RESET
#  define EH_CP_SDIO_TX_QUEUE_SIZE      CONFIG_EH_TRANSPORT_CP_SDIO_TX_Q_SIZE
#  define EH_CP_SDIO_RX_QUEUE_SIZE      CONFIG_EH_TRANSPORT_CP_SDIO_RX_Q_SIZE
#  ifdef CONFIG_EH_TRANSPORT_CP_SDIO_HIGH_SPEED
#    define EH_CP_SDIO_HIGH_SPEED       1
#    define EH_CP_SDIO_CLOCK_FREQ_KHZ   40000
#  else
#    define EH_CP_SDIO_HIGH_SPEED       0
#    define EH_CP_SDIO_CLOCK_FREQ_KHZ   20000
#  endif
/* Stream-class modes (STREAM now; SW_AGGR later) set the legacy flag=1 —
 * the 0x18 TLV stays a 2-state stream/packet indicator on the wire. */
#  if defined(CONFIG_EH_TRANSPORT_CP_SDIO_MODE_STREAM) || \
      defined(CONFIG_EH_TRANSPORT_CP_SDIO_MODE_SW_AGGR)
#    define EH_CP_SDIO_STREAMING_MODE   1
#  else
#    define EH_CP_SDIO_STREAMING_MODE   0
#  endif
/* eh_sdio_cfg_tx_mode value for the 0x1B buf-config TLV (eh_common_sdio_cfg.h). */
#  if defined(CONFIG_EH_TRANSPORT_CP_SDIO_MODE_SW_AGGR)
#    define EH_CP_SDIO_TX_MODE          EH_SDIO_CFG_TXMODE_SW_AGGR
#    define EH_CP_SDIO_SW_AGGR          1
#  elif defined(CONFIG_EH_TRANSPORT_CP_SDIO_MODE_STREAM)
#    define EH_CP_SDIO_TX_MODE          EH_SDIO_CFG_TXMODE_STREAM
#    define EH_CP_SDIO_SW_AGGR          0
#  else
#    define EH_CP_SDIO_TX_MODE          EH_SDIO_CFG_TXMODE_PACKET
#    define EH_CP_SDIO_SW_AGGR          0
#  endif
#  if CONFIG_EH_TRANSPORT_CP_SDIO_PSEND_PSAMPLE
#    define EH_CP_SDIO_SAMPLING_PHASE   "psend_psample"
#    define EH_CP_SDIO_TIMING           SDIO_SLAVE_TIMING_PSEND_PSAMPLE
#  elif CONFIG_EH_TRANSPORT_CP_SDIO_NSEND_PSAMPLE
#    define EH_CP_SDIO_SAMPLING_PHASE   "nsend_psample"
#    define EH_CP_SDIO_TIMING           SDIO_SLAVE_TIMING_NSEND_PSAMPLE
#  elif CONFIG_EH_TRANSPORT_CP_SDIO_PSEND_NSAMPLE
#    define EH_CP_SDIO_SAMPLING_PHASE   "psend_nsample"
#    define EH_CP_SDIO_TIMING           SDIO_SLAVE_TIMING_PSEND_NSAMPLE
#  elif CONFIG_EH_TRANSPORT_CP_SDIO_NSEND_NSAMPLE
#    define EH_CP_SDIO_SAMPLING_PHASE   "nsend_nsample"
#    define EH_CP_SDIO_TIMING           SDIO_SLAVE_TIMING_NSEND_NSAMPLE
#  else
#    define EH_CP_SDIO_SAMPLING_PHASE   "default"
#    define EH_CP_SDIO_TIMING           SDIO_SLAVE_TIMING_PSEND_PSAMPLE
#  endif
#endif

#endif /* EH_CP_CFG_BUS_SDIO_H */
