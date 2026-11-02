# Thread Border Router — RCP on a separate chip (`openthread/rcp_separate_chip/border_router`)

<!-- tags: openthread, thread, border-router, wifi, ble, nimble, rcp, esp32p4, esp32c5, esp32h2, esp32h21 -->

<!-- common-start -->
A Thread Border Router built from three chips. Each chip does one job:

| Role | Job | Firmware | Tested with |
|---|---|---|---|
| Host | Runs the application: OpenThread Border Router, NimBLE, TCP/IP | `esp_host/` | ESP32-P4 |
| Co-processor (CP) | Wi-Fi station and Bluetooth LE radio, over ESP-Hosted | `cp/` | ESP32-C5 |
| Radio Co-Processor (RCP) | IEEE 802.15.4 (Thread) radio, over spinel | `cp_rcp/` | ESP32-H21 (ESP32-H2 is the main target) |

## The big picture

```text
                 Wi-Fi AP / LAN                              Thread devices
                       ^                                           ^
                       | Wi-Fi + BLE                               | 802.15.4
          +------------+------------+                 +------------+-------------+
          |   Co-processor (CP)     |                 | Radio Co-Processor (RCP) |
          |   Wi-Fi + BLE radio     |                 | 802.15.4 radio           |
          +------------+------------+                 +------------+-------------+
                       |                                           |
             ESP-Hosted bus                                  UART (spinel)
             SDIO / SPI / UART                               dedicated link
                       |                                           |
          +------------+-------------------------------------------+-------------+
          |                                Host                                  |
          |      OpenThread Border Router  ·  NimBLE  ·  lwIP  ·  console        |
          +----------------------------------------------------------------------+
```

- **Wi-Fi and BLE** go through ESP-Hosted. The host uses the normal `esp_wifi` and NimBLE APIs. The CP does the radio work.
- **Thread** goes through a separate UART. The host runs the full OpenThread stack. The RCP runs the ESP-IDF `ot_rcp` firmware.
- The two links are independent. The host routes IP traffic between the Thread network and the Wi-Fi network.

## Supported chips and buses

The chips, pins and bus in this README are one example setup. You can use other combinations from the tables below.

### Host

| Host | ESP32-P4 | Other ESP chips | Other MCUs | Linux |
| :--: | :------: | :-------------: | :--------: | :---: |
| Support | Yes (tested) | Not tested | No | No |

The host app is an ESP-IDF application. It needs PSRAM and a free UART for spinel.

### Co-processor (Wi-Fi + BLE)

| CP | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-C3 | ESP32-S3 | ESP32 |
| :-: | :------: | :------: | :-------: | :------: | :------: | :---: |
| Support | Yes (tested) | Yes | Yes | Yes | Yes | Yes |

You can use any ESP-Hosted co-processor that has Wi-Fi and Bluetooth LE.

### Radio Co-Processor (Thread)

| RCP | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-C6 | ESP32-C5 |
| :-: | :------: | :-------: | :------: | :------: | :------: |
| Support | Yes (main target) | Yes (tested, see note) | Yes | Yes | Yes |

Any ESP chip with an IEEE 802.15.4 radio can be the RCP. ESP32-H2 is the main target. The bench used an ESP32-H21; from the ESP-Hosted side, the two behave the same.

> [!NOTE]
> ESP32-H21 support in ESP-IDF is a preview. With the PHY library in ESP-IDF v6.1, the H21 receives almost no 802.15.4 frames. Use an ESP-IDF version with the updated ESP32-H21 PHY library, or use another RCP chip.

> [!WARNING]
> On ESP-IDF master, an ESP32-P4 host older than v3 (for example v1.3) panics with `Illegal instruction` when the border router starts. The prebuilt `components/openthread/lib/esp32p4/libopenthread_br.a` uses Zba/Zbb instructions, which only ESP32-P4 v3 has. On an older ESP32-P4, use ESP-IDF v6.1.

### Buses

| Link | SDIO | SPI Full-Duplex | SPI Half-Duplex | UART |
| :--- | :--: | :-------------: | :-------------: | :--: |
| Host to CP (ESP-Hosted) | Yes (tested) | Yes | Yes | Yes |
| Host to RCP (spinel) | No | No | No | Yes (tested) |

## Tested setup

| Node | Needed | When |
|---|---|---|
| ESP32-P4 + ESP32-C5 core board | Always | Host and CP. The board connects them over SDIO. |
| ESP32-H21 board | Always | RCP. Connect it to the P4 with three wires, plus an optional reset wire. |
| Wi-Fi access point | Always | Backbone network for the border router. |
| Thread device (any 802.15.4 ESP board with ESP-IDF `ot_cli`) | Optional | To send traffic through the Thread network. |
| Phone or computer with BLE | Optional | To connect to the GATT service. |
| Computer on the same LAN | Optional | To ping the host over Wi-Fi. |

### Connections

Host to CP: SDIO, 4-bit, 40 MHz. These are traces on the core board, so you connect nothing:

```text
   ESP32-P4 (host)                       ESP32-C5 (CP)
   GPIO 50  CLK   ---------------------  GPIO 9   CLK
   GPIO 51  CMD   ---------------------  GPIO 10  CMD
   GPIO 49  D0    ---------------------  GPIO 8   D0
   GPIO 48  D1    ---------------------  GPIO 7   D1
   GPIO 53  D2    ---------------------  GPIO 14  D2
   GPIO 52  D3    ---------------------  GPIO 13  D3
   GPIO 54  Reset -------------------->  RST
```

Host to RCP: UART, 460800 baud, 8N1, no flow control. Connect these three wires:

```text
   ESP32-P4 (host)                       ESP32-H21 (RCP)
   GPIO 4   UART1 TX  ---------------->  GPIO 6  RX
   GPIO 6   UART1 RX  <----------------  GPIO 5  TX
   GND                -----------------  GND
   GPIO 7   (optional) --------------->  EN (chip reset; RST on DevKits)
```

The reset wire is optional. It goes to the RCP's EN (chip enable / reset) pin, not to a GPIO: the RCP firmware does not read it. With it, the host pulls EN low to reset the RCP when OpenThread reports an RCP failure. To use it, set `EXAMPLE_RCP_RESET_PIN` to the host GPIO (see the Configuration section).

For other chips or pins, change the host and RCP UART settings together (see the Configuration section).

## What you see after boot

The host log shows one line per link:

```text
I (5127)  demo: thread: RCP openthread-esp32/...; esp32h21; ...
I (9077)  ble_prph: scan: done, 16 devices
I (27817) demo: wifi: station 192.168.21.230, gateway 192.168.20.1
I (27817) demo: wifi: AP "<your AP>" channel 48 (5 GHz), rssi -70, phy n
I (32817) demo: wifi: ping gateway 5/5 replies, 91 ms total
I (35127) demo: thread: role leader, channel 15, panid 0x1234, rloc16 0x6400, children 0
```

| Link | What the example does |
|---|---|
| Wi-Fi (CP) | Connects as a station, then pings the gateway 5 times |
| BLE (CP) | Scans for 5 s and logs up to 16 devices, then advertises as `esp-ot-br-3chip` with a GATT service |
| Thread (RCP) | Reads the RCP version, forms a Thread network, then logs the role and child count every 30 s |
| Border routing | Routes between the Thread network and the Wi-Fi LAN (NAT64, mDNS) |

## Build and flash

<!-- common-ignore-start -->
Set up the tools once, from the repository root:

```bash
./install.sh      # install.fish for the fish shell
. ./export.sh     # . ./export.fish for the fish shell
```
<!-- common-ignore-stop -->

Flash the three chips in this order: RCP, CP, host.
<!-- common-stop -->

<!-- coprocessor-start -->
### 1. RCP

```bash
cd examples/openthread/rcp_separate_chip/border_router/cp_rcp
eh.py set-target esp32h21         # or esp32h2, esp32h4, esp32c6, esp32c5
eh.py -p <rcp_port> erase-flash flash
```

### 2. CP

On the ESP32-P4 + ESP32-C5 core board, add the board file `sdkconfig.defaults.p4-c5-core-board`:

```bash
cd examples/openthread/rcp_separate_chip/border_router/cp
eh.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c5;sdkconfig.defaults.p4-c5-core-board" set-target esp32c5
eh.py build
```

On this board, you can flash the C5 only while the P4 is in download mode. Put the P4 in download mode first, then flash the C5:

```bash
esptool.py -p <host_port> --before default_reset --after no_reset chip_id
eh.py -p <cp_port> flash
```

For a different CP chip, set that target and leave out the board file. Select the same bus on the CP and the host in `eh.py menuconfig`.
<!-- coprocessor-stop -->

<!-- esp_host-start -->
### 3. Host

On the ESP32-P4 + ESP32-C5 core board, add the board file `sdkconfig.defaults.p4-c5-core-board`:

```bash
cd examples/openthread/rcp_separate_chip/border_router/esp_host
eh.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.p4-c5-core-board" set-target esp32p4
eh.py menuconfig
```

Set your Wi-Fi AP name and password:

```text
Example Connection Configuration
├── WiFi SSID
└── WiFi Password
```

Then flash and open the console:

```bash
eh.py -p <host_port> flash monitor
```
<!-- esp_host-stop -->

<!-- common-start -->
## Try it

The host console prompt is `esp32p4>`. All OpenThread CLI commands start with `ot`.

1. **Thread.** Check the network and print the dataset:

   ```text
   esp32p4> ot state
   esp32p4> ot dataset active -x
   ```

   To add a Thread device, flash the ESP-IDF `ot_cli` example on another 802.15.4 board. On that board, run `ot dataset set active <hex>`, `ot ifconfig up` and `ot thread start`. The device joins the border router. From the device, `ot ping 8.8.8.8` goes through the RCP, the host and the Wi-Fi link to the internet.

2. **BLE.** Scan with a phone app (for example nRF Connect) and connect to `esp-ot-br-3chip`. The service has two characteristics:
   - `5c3a659e-897e-45e1-b016-007107c96df6`: read returns a random number.
   - `5c3a659e-897e-45e1-b016-007107c96df7`: read and write one byte.

3. **Wi-Fi.** From a computer on the same LAN, ping the address in the `wifi: station` log line.

## How it works

Boot order on the host (`esp_host/main/esp_ot_br.c`):

```text
app_main
 ├─ esp_hosted_init + connect            bring up the bus to the CP
 ├─ ble_prph_start                       NimBLE over ESP-Hosted HCI: scan, then advertise
 ├─ esp_openthread_start                 OpenThread over the spinel UART to the RCP
 ├─ app_demo_start                       Wi-Fi ping on got-IP, Thread status task
 ├─ esp_openthread_border_router_start   connect Wi-Fi, use it as the backbone
 └─ ot_network_auto_start                form or join the Thread network from Kconfig
```

The RCP is not an ESP-Hosted co-processor. So the host does not use the ESP-Hosted OpenThread feature. `esp_ot_config.h` sets `ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()` to a UART radio config from the example Kconfig, as in ESP-IDF `ot_br`.

Compare with the two-chip examples in `openthread/rcp_on_hosted_cp/`:

| Example | Wi-Fi radio | 802.15.4 radio | Host starts the RCP over ESP-Hosted |
|---|---|---|---|
| `rcp_on_hosted_cp/border_router`, `rcp_on_hosted_cp/cli` | CP (ESP32-C6) | Same CP | Yes |
| `rcp_separate_chip/border_router` | CP | Separate RCP chip | No |

## Configuration

These settings make the three-chip setup work. They are the defaults in `sdkconfig.defaults` and in the example Kconfig.

| Where | Setting | Value | Why |
|---|---|---|---|
| Host | `EXAMPLE_HOST_UART_PORT` | `1` | UART1 to the RCP |
| Host | `EXAMPLE_HOST_UART_TX_PIN`, `EXAMPLE_HOST_UART_RX_PIN` | `4`, `6` | Spinel UART pins |
| Host | `EXAMPLE_HOST_UART_BAUDRATE` | `460800` | Same as the RCP firmware |
| Host | `EXAMPLE_RCP_RESET_PIN` | `-1` | Host GPIO to the RCP reset input. `-1`: no hardware reset; the host logs a warning at boot |
| Host | `ESP_HOSTED_HOST_FEAT_BT`, `BT_NIMBLE_ENABLED` | `y` | NimBLE host over ESP-Hosted |
| Host | `OPENTHREAD_NETWORK_AUTO_START` | `y` | Form the network at boot |
| Host | `OPENTHREAD_CLI_WIFI` | `n` | Conflicts with auto start |
| Host | `EXAMPLE_WIFI_CONN_MAX_RETRY` | `-1` | Retry Wi-Fi forever; the border router helper stops the app if Wi-Fi fails |
| Host | `LWIP_TCP_WND_DEFAULT`, `LWIP_TCP_SND_BUF_DEFAULT` | `65534` | The default 5760-byte window limits TCP to a few Mbit/s |
| CP | `ESP_HOSTED_CP_FEAT_WIFI`, `ESP_HOSTED_CP_FEAT_BT` | `y` | Wi-Fi and BLE only, no OpenThread |
| RCP | `EXAMPLE_RCP_UART_RX_PIN`, `EXAMPLE_RCP_UART_TX_PIN` | `6`, `5` | Spinel UART pins |

The host partition table (`esp_host/partitions.csv`) has two OTA slots of 1920 KB each.

## Test results

Test setup. Nodes in brackets are optional test equipment, not part of the solution:

```text
                                       Internet
                                          |
    [Computer]  ~~~~~ LAN ~~~~~~~~~~  Wi-Fi AP (5 GHz)
     ping, BLE central                    ^
         :                                | Wi-Fi
         : BLE                            |
  +------v----------- P4 + C5 core board -+----------+
  |   ESP32-P4 (host) <--- SDIO ---> ESP32-C5 (CP)   |
  +------+-------------------------------------------+
         |
         | UART (spinel)
         |
  +------+-----------+      802.15.4, channel 15      +----------------------+
  | ESP32-H21 (RCP)  | <----------------------------> | [ESP32-C6 + ot_cli]  |
  +------------------+                                |  Thread device       |
                                                      +----------------------+
```

Firmware: ESP-IDF v6.1. ESP32-H21 PHY library `101,8, 736c80c` (Sep 23 2026). The Thread device joins with the dataset from `ot dataset active -x` on the host.

| Test | Result |
|---|---|
| Spinel UART, host to RCP | RCP version read at boot |
| Thread network | Host forms the network as leader on channel 15 |
| 802.15.4 receive (`ot scan`) | 7 nearby Thread networks found, channels 11 to 25 |
| Thread device joins | Attaches as a child in 1 s, link quality 3 in and out |
| Thread device to border router | `ot ping ff03::1`: 1/1 |
| Thread device to Wi-Fi gateway (NAT64) | `ot ping 192.168.20.1`: 10/10, 0 % loss, average 74 ms |
| Thread device to internet (NAT64) | `ot ping 8.8.8.8`: 5/5, 0 % loss, average 61 ms |
| Thread, 512-byte packets (fragmented) | `ot ping ff02::1 512 5`: 5/5, average 217 ms |
| Wi-Fi station | IP address received; gateway ping 5/5 |
| LAN to host | Ping 5/5 |
| BLE scan | 16 devices logged |
| BLE GATT (computer as central) | Connect, MTU 256, 2 reads, 2 write and read-back checks: all pass |
| ESP-Hosted bus, raw | Host to CP: 85.6 Mbit/s; CP to host: 78.6 Mbit/s |
| TCP, host to LAN (5 GHz, RSSI −37) | 34.1 Mbit/s with BLE stopped; 5.2 Mbit/s with BLE scanning and advertising |
<!-- common-stop -->

## Thread throughput

Host to Thread device, UDP: about 43 kbit/s with the defaults, and about 52 kbit/s with the spinel UART at 921600 baud and the RCP built with `-O2`. Measurements, per-frame timing and how to change the settings: [THROUGHPUT.md](THROUGHPUT.md).

## Directory layout

```text
openthread/rcp_separate_chip/border_router/
├── cp/          CP firmware: ESP-Hosted co-processor, Wi-Fi + BLE
├── cp_rcp/      RCP firmware: ESP-IDF ot_rcp with the spinel UART pins
└── esp_host/    Host firmware: Border Router + BLE + demo
```

More information:

- [OpenThread feature in ESP-Hosted](../../../../docs/features/openthread.md)
- [Getting Started: MCU](../../../../docs/getting-started-mcu.md) (bus wiring and pull-ups)
- [ESP-IDF `ot_br` example](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_br)
- [ESP-IDF `ot_rcp` example](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_rcp)
- [ESP-IDF `ot_cli` example](https://github.com/espressif/esp-idf/tree/master/examples/openthread/ot_cli)
