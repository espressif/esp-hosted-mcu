# Thread throughput: separate-chip RCP

Measured Thread throughput for this example, and the settings that change it.

## Setup

| Item | Value |
|---|---|
| Host | ESP32-P4 (P4 + C5 core board), this example's `esp_host`, ESP-IDF v6.1 |
| RCP | ESP32-H21, this example's `cp_rcp`, ESP-IDF v6.1, PHY library `a9f2a15` |
| Thread peer | ESP32-H4 rev v0.2, ESP-IDF master `ot_cli` |
| Channel | 26 (on this bench, channel 15 had CCA failures and retries that changed results run to run) |
| Traffic | `iperf` UDP over Thread (mesh-local addresses), 512-byte datagrams, 100 kbit/s offered, 15 s |
| Result | Average of the three 5-second receiver intervals during the send |
| Runs | 2 per row; both runs are shown |

## Results

Host to Thread peer (traffic sent by the RCP):

| RCP build | Spinel UART baud | Received kbit/s (run 1 / run 2) | 802.15.4 frames sent | Change |
|---|---|---|---|---|
| Default (`-Os`) | 460800 | 43.4 / 42.1 | 1070 / 1053 | — |
| `-O2` | 460800 | 45.1 / 45.6 | 1112 / 1116 | +6% |
| `-O2` | 921600 | 51.6 / 51.9 | 1262 / 1258 | +21% |
| `-O2` | 2000000 | 52.4 / 52.7 | 1271 / 1274 | +23% |

Change is against the average of the two default runs. No CCA failures. One frame in all eight runs was not acknowledged (default build, run 2).

Thread peer to host (traffic received by the RCP): 44–47 kbit/s with every RCP build and baud rate above, 1015–1081 frames per run. The RCP settings do not change this direction.

For comparison, the RCP on the ESP32-C5 over SDIO (example `rcp_on_hosted_cp/cli` on the P4, ESP-IDF master, same peer and channel; a different setup, not a like-for-like comparison) reached 81 kbit/s host to peer (1897 frames).

## What limits it

The host and the RCP use OpenThread's spinel protocol, which sends one frame at a time: the host sends a frame to the RCP and waits for the RCP to report the transmission done before it sends the next one. Every frame pays the full round trip.

Measured with timestamps on both chips, default build at 460800 baud:

| Step | Time per frame |
|---|---|
| Radio: CSMA, about 135 bytes on air, ACK | 4.85 ms |
| RCP: last byte of the frame read from UART → radio transmit started | 2.4 ms |
| RCP: transmit done → "done" reply written to UART | 0.6 ms |
| Host: "done" reply read → next frame written | under 0.3 ms |
| UART: 135-byte frame at 460800 | about 3 ms |
| UART: 48-byte "done" reply at 460800 | about 1 ms |

These steps do not add up to the full frame interval. The rest is not attributed yet.

The baud results show that the UART is part of the cost up to 921600. Above 921600, a faster UART gives no further gain.

These settings had no effect on throughput: host FreeRTOS tick (100 or 1000 Hz), host OpenThread task priority (5 or 23), and host UART RX FIFO threshold and timeout.

## Raise the spinel UART baud rate

Change both sides to the same value:

1. RCP: `.baud_rate` in `cp_rcp/main/esp_ot_config.h`
2. Host: `EXAMPLE_HOST_UART_BAUDRATE` in `idf.py menuconfig`

921600 gives most of the gain. It needs short wires between the P4 and the RCP.

## Build the RCP with `-O2`

Add to `cp_rcp/sdkconfig.defaults`, or set in `idf.py menuconfig`:

```text
CONFIG_COMPILER_OPTIMIZATION_SIZE=n
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

The RCP app grows from 260400 to 281248 bytes. This build ran at 96 MHz, the highest CPU frequency the ESP32-H21 offers in ESP-IDF.
