/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef __EH_NW_SPLIT_LWIP_SRC_PORT_HOOK_H__
#define __EH_NW_SPLIT_LWIP_SRC_PORT_HOOK_H__

#include "sdkconfig.h"

/* Network split lwIP port configuration. */
/* Network split gives the host and the CP disjoint local port ranges. Each side
 * routes an inbound packet to a stack by its destination port, so a local port
 * taken from the peer half loses every reply.
 *
 * lwIP does not read these Kconfig values. It uses its own default 49152-65535
 * unless the range macros are set. tools/cmake/eh_lwip_port_range.cmake force
 * includes this file into every lwIP file to set them.
 *
 * Reserved ports (generally <1024) like DHCP stay outside the ranges. They are
 * hardcoded, so they keep working.
 */

#define ENSURE_PORT_RANGE(port, START, END) \
    (((port) >= (START) && (port) <= (END)) ? \
        (port) : \
        (((port) % ((END) - (START) + 1)) + (START)))

/* TCP only: seed below END. tcp_new_port() increments before it tests for END,
 * so a seed of END becomes END+1 and escapes the range. udp_new_port() tests
 * first and needs no guard.
 */
#define TCP_ENSURE_SEED_PORT(port, START, END) ENSURE_PORT_RANGE(port, START, (END) - 1)

#ifdef CONFIG_LWIP_TCP_LOCAL_PORT_RANGE_START
#define TCP_LOCAL_PORT_RANGE_START CONFIG_LWIP_TCP_LOCAL_PORT_RANGE_START
#define TCP_LOCAL_PORT_RANGE_END   CONFIG_LWIP_TCP_LOCAL_PORT_RANGE_END
#define TCP_ENSURE_LOCAL_PORT_RANGE(port) TCP_ENSURE_SEED_PORT(port, TCP_LOCAL_PORT_RANGE_START, TCP_LOCAL_PORT_RANGE_END)
#if CONFIG_LWIP_TCP_LOCAL_PORT_RANGE_END == 0xffff
  #define IS_LOCAL_TCP_PORT(port) (port>=TCP_LOCAL_PORT_RANGE_START)
#else
  #define IS_LOCAL_TCP_PORT(port) (port>=TCP_LOCAL_PORT_RANGE_START && (port<=CONFIG_LWIP_TCP_LOCAL_PORT_RANGE_END))
#endif
#endif

#ifdef CONFIG_LWIP_TCP_REMOTE_PORT_RANGE_START
#define TCP_REMOTE_PORT_RANGE_START CONFIG_LWIP_TCP_REMOTE_PORT_RANGE_START
#define TCP_REMOTE_PORT_RANGE_END   CONFIG_LWIP_TCP_REMOTE_PORT_RANGE_END
#if CONFIG_LWIP_TCP_REMOTE_PORT_RANGE_END == 0xffff
  #define IS_REMOTE_TCP_PORT(port) (port>=TCP_REMOTE_PORT_RANGE_START)
#else
  #define IS_REMOTE_TCP_PORT(port) (port>=TCP_REMOTE_PORT_RANGE_START && (port<=CONFIG_LWIP_TCP_REMOTE_PORT_RANGE_END))
#endif
#endif

#ifdef CONFIG_LWIP_UDP_LOCAL_PORT_RANGE_START
#define UDP_LOCAL_PORT_RANGE_START CONFIG_LWIP_UDP_LOCAL_PORT_RANGE_START
#define UDP_LOCAL_PORT_RANGE_END   CONFIG_LWIP_UDP_LOCAL_PORT_RANGE_END
#define UDP_ENSURE_LOCAL_PORT_RANGE(port) ENSURE_PORT_RANGE(port, UDP_LOCAL_PORT_RANGE_START, UDP_LOCAL_PORT_RANGE_END)
#if CONFIG_LWIP_UDP_LOCAL_PORT_RANGE_END == 0xffff
  #define IS_LOCAL_UDP_PORT(port) (port>=UDP_LOCAL_PORT_RANGE_START)
#else
  #define IS_LOCAL_UDP_PORT(port) (port>=UDP_LOCAL_PORT_RANGE_START && (port<=CONFIG_LWIP_UDP_LOCAL_PORT_RANGE_END))
#endif
#define DNS_PORT_ALLOWED(port) IS_LOCAL_UDP_PORT(port)
#endif

#ifdef CONFIG_LWIP_UDP_REMOTE_PORT_RANGE_START
#define UDP_REMOTE_PORT_RANGE_START CONFIG_LWIP_UDP_REMOTE_PORT_RANGE_START
#define UDP_REMOTE_PORT_RANGE_END   CONFIG_LWIP_UDP_REMOTE_PORT_RANGE_END
#if CONFIG_LWIP_UDP_REMOTE_PORT_RANGE_END == 0xffff
  #define IS_REMOTE_UDP_PORT(port) (port>=UDP_REMOTE_PORT_RANGE_START)
#else
  #define IS_REMOTE_UDP_PORT(port) (port>=UDP_REMOTE_PORT_RANGE_START && (port<=CONFIG_LWIP_UDP_REMOTE_PORT_RANGE_END))
#endif
#endif

#endif /* __EH_NW_SPLIT_LWIP_SRC_PORT_HOOK_H__ */
