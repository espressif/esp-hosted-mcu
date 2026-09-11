# rf_cert (`examples/rf_cert/`)

[Feature page](../../docs/features/rf-cert-test.md) · [Examples](../README.md)

<!-- common-start -->

Drive the co-processor's PHY certification test APIs from a console on the
host. You type each step and read each result, so the sweep runs at your
pace, not the firmware's.

> [!CAUTION]
> For conducted RF measurement only. Do not ship either firmware. A host
> can put the radio into continuous transmit, which is a regulatory
> problem. The feature is off by default.

---

## Before you start

If the co-processor's UART and boot pin are reachable on your board, flash
Espressif's RF test firmware to it and use EspRFTestTool instead. That tool
covers more test items, and ESP-Hosted takes no part.

This example is for the other case: the co-processor is only reachable through
the host. EspRFTestTool cannot drive such a board, because it flashes a
complete image over the co-processor's own UART in ROM download mode, which no
hosted link can carry.

---

## How the two sides split

The co-processor is a **server**. It answers the RF cert RPCs and starts
nothing by itself. Which commands exist there follows the chip: Wi-Fi
needs `SOC_WIFI_SUPPORTED`, BLE needs `SOC_BT_SUPPORTED`, and 802.11ax
needs `SOC_WIFI_HE_SUPPORT`.

The **host** owns the test. It brings up the link, then gives you a
`phy>` console — the same prompt cert_test uses.

```
  You ──type──> host console ──RPC──> co-processor ──> PHY ──> antenna
                                                              |
                                          spectrum analyser <─┘
```

<!-- common-stop -->

---

## Build and flash

<!-- coprocessor-start -->

Co-processor:

```
cd cp
idf.py set-target esp32c6
idf.py flash monitor
```

Any co-processor target works. `sdkconfig.defaults.<target>` files hold
tuning for esp32, esp32c2, esp32c3, esp32c5, esp32c6, esp32c61, esp32s2
and esp32s3. Other targets build from the base defaults — ESP32-H2, for
example, builds and gates itself to the BLE commands.

<!-- coprocessor-stop -->

<!-- esp_host-start -->

Host:

```
cd mcu_host
idf.py set-target esp32p4
idf.py flash monitor
```

The host prints a command summary at start-up. `help` lists everything.

<!-- esp_host-stop -->

---

<!-- common-start -->

## Order of a run

```
phy_cert_query                 # is the co-processor able to do this?
phy_cert_init                   # enter cert mode — do this first
  ... start a test, measure, cmdstop ...   repeat as needed
phy_cert_deinit                 # optional: stop and power the radio down
```

`cmdstop` ends the running test. `phy_cert_deinit` also powers the
Wi-Fi domain down, which is tidy when you finish a session.

The host enters cert mode at start-up, as the ESP-IDF example does from
`app_main`, so `phy_cert_init` is only needed after a `phy_cert_deinit`.

Neither one leaves cert mode. **Restart the co-processor for that** — the
PHY keeps its cert state once entered, and there is no command to clear it.

---

## Wi-Fi scenarios

**Continuous transmit** — the usual conducted TX-power and spurious sweep.
`-c 0` means never stop.

```
tx_contin_en -e 1
esp_tx -n 1 -r 0 -p 0 -l 1000 -d 1000 -c 0
  ... measure ...
cmdstop
```

**A fixed burst** — 1000 packets instead of forever.

```
esp_tx -n 6 -r 0x0b -l 1000 -d 1000 -c 1000
```

**Receive sensitivity / PER** — send known packets from the tester, then
read the counts. `get_rx_result` prints correct, total, RSSI and the
packet error rate.

```
esp_rx -n 1 -r 0
  ... tester sends N packets ...
cmdstop
get_rx_result
```

**Carrier wave** — a single unmodulated tone for frequency-error and
occupied-bandwidth checks.

```
wifiscwout -e 1 -c 1 -p 0
  ... measure ...
wifiscwout -e 0
```

**HT40** — before starting an 11n test.

```
cbw40m_en -e 1
```

**802.11ax** — ESP32-C5, C6 and C61 only. Returns an error elsewhere.

```
phy_11ax_tx_set -f 0 -p 0 -g 0 -i 0
esp_tx -n 1 -r 0x20 -l 1000 -c 0
```

---

## BLE scenarios

These drive the PHY directly. They do **not** use the Bluetooth
controller or any host stack, so they are a different measurement from
HCI Direct Test Mode.

Channel numbering differs between transmit and receive, as it does in ESP-IDF:

| Command | Channel 0..39 means |
| :--- | :--- |
| `esp_ble_tx`, `bt_tx_tone` | frequency = 2402 + chan×2 MHz, so 0 is 2402 MHz |
| `esp_ble_rx` | BLE data-channel index: 0 is 2404 MHz, 37 is 2402 MHz, 38 is 2426 MHz, 39 is 2480 MHz |

**Transmit** — PRBS9 payload, 37 bytes, the usual instrument default.

```
esp_ble_tx -p 8 -n 0 -l 37 -t 2 -s 0x71764129 -r 0 -m 0
  ... measure ...
cmdstop
```

**Receive / PER**

```
esp_ble_rx -n 0 -s 0x71764129 -r 0
  ... tester sends N packets ...
cmdstop
get_rx_result
```

**Carrier wave**

```
bt_tx_tone -e 1 -n 0 -p 0
  ... measure ...
bt_tx_tone -e 0
```

**Coded PHY** — `-r 2` selects 125 kbps, `-r 3` selects 500 kbps.

```
esp_ble_tx -n 0 -r 2 -m 0
```

---

## Options

Command names and option letters are those of the ESP-IDF `phy/cert_test`
example, so commands and scripts you already have carry over:

```
esp_tx -n 1 -r 0 -p 0 -l 1000 -d 1000 -c 0
```

`fcc_le_tx` and `rw_le_rx_per` are registered as further names for
`esp_ble_tx` and `esp_ble_rx`. Every command also answers to a
`phy_cert_`-prefixed name, which keeps it findable in a hosted console that
carries other features.

`gpio_output_set` drives a pin on the co-processor, as it does in the
ESP-IDF example, so it needs the GPIO expander feature. Both example
configurations enable it. Without it the command is not registered and the
rest still works.

| Option | Meaning |
| :--- | :--- |
| `-n` | channel — Wi-Fi 1..14, BLE 0..39 (see the BLE table above) |
| `-r` | rate |
| `-p` | power attenuation in 0.25 dB steps (Wi-Fi/tone), or power level (BLE TX) |
| `-l` | payload length in bytes |
| `-d` | inter-packet gap in µs |
| `-c` | Wi-Fi packet count, or tone channel — see below |
| `-m` | BLE packet count |
| `-e` | 1 starts, 0 stops |
| `-s` | BLE sync word |
| `-t` | BLE payload pattern; 2 is PRBS9 |
| `-f` `-g` `-i` | 802.11ax HE format, GI/LTF, RU index |

A count of `0` means transmit continuously until `cmdstop`.

`-c` means two things, as it does in ESP-IDF: a packet count for
`esp_tx`, and the channel for `wifiscwout`.

<!-- common-stop -->

<!-- common-start -->

## Watchdog

`CONFIG_ESP_TASK_WDT_EN=n` is set in the co-processor defaults. A long
continuous transmit leaves the idle task no work and would otherwise trip
the task watchdog. The ESP-IDF `phy/cert_test` example does the same.

<!-- common-stop -->

