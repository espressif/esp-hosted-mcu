# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Power-save wake-on-packet tests: one UDP datagram must wake the host and reach it.

The CP reserves the wake port for the host (only reserved ports wake a sleeping
host); lwIP's UDP debug logs every datagram the host stack takes, so delivery is
proven without app code. A payload length per wake makes each datagram's log line
unique."""

CP_OVL = ('CONFIG_ESP_HOSTED_HOST_UDP_DST_PORTS="60000"',)
HOST_OVL = ('CONFIG_LWIP_DEBUG=y', 'CONFIG_LWIP_UDP_DEBUG=y')


def payload(n):
    """The datagram for wake n (0-based)."""
    return b'w' * (16 + n)


def rx_line(n):
    """lwIP udp_input's log for wake n's datagram (8-byte UDP header + payload)."""
    return rf'udp_input: received datagram of length {8 + len(payload(n))}\b'
