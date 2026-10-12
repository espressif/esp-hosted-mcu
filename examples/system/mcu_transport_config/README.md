# Manual bring-up (`examples/system/mcu_transport_config/`)

<!-- common-start -->
Sets the bus clock from the application (no auto-init): start the link at one clock,
then stop it and start it again at another. All of it is in `main.c`; Wi-Fi, BLE and
a link monitor are optional extras in their own files.

Use it when the application, not menuconfig, decides the bus settings (for example
from a board ID or NVS), or when you want to control when Wi-Fi and BLE start.

### Changing the bus settings

```mermaid
sequenceDiagram
    participant App
    participant H as ESP-Hosted
    App->>H: esp_hosted_sdio_set_config(&cfg)  [20 MHz]
    H-->>App: ESP_TRANSPORT_OK
    App->>H: esp_hosted_init() + esp_hosted_connect_to_slave()
    Note over H: link up at 20 MHz
    App->>H: esp_hosted_sdio_set_config(&cfg)  [40 MHz]
    H-->>App: ESP_TRANSPORT_ERR_ALREADY_SET
    App->>H: esp_hosted_deinit()
    App->>H: esp_hosted_sdio_set_config(&cfg)  [40 MHz]
    H-->>App: ESP_TRANSPORT_OK
    App->>H: esp_hosted_init() + esp_hosted_connect_to_slave()
    Note over H: link up at 40 MHz
```

```c
struct esp_hosted_sdio_config cfg = INIT_DEFAULT_HOST_SDIO_CONFIG();
cfg.clock_freq_khz = 40000;
esp_hosted_sdio_set_config(&cfg);   // before esp_hosted_init(), or after esp_hosted_deinit()
esp_hosted_init();
esp_hosted_connect_to_slave();      // log: bus applied: clk_khz=40000
```

- Settings can change only while the link is down. While it is up, `set_config`
  returns `ESP_TRANSPORT_ERR_ALREADY_SET`.
- Without `set_config`, the menuconfig values are used.
- The same works for SPI, SPI half duplex and UART (`esp_hosted_spi_set_config()`,
  `esp_hosted_spi_hd_set_config()`, `esp_hosted_uart_set_config()`).

| Bus | First start | After restart | Fixed (rejected if changed) |
| :--- | :--- | :--- | :--- |
| SDIO | 20000 kHz | 40000 kHz | `rx_mode`, `block_mode` |
| SPI | 20 MHz | 26 MHz | — |
| SPI half duplex | 20 MHz | 40 MHz | command/address/dummy bits (8) |
| UART | 921600 | 921600 (must match the co-processor) | — |

## Supported Platforms and Transports

### Supported Coprocessors

| Coprocessor | ESP32 | ESP32-C Series | ESP32-S Series |
| :----------: | :---: | :------------: | :------------: |
| Support     | Yes   | Yes            | Yes            |

### Supported Host Devices

| Host Device | ESP32-P4 | ESP32-H2 | Other MCUs | Linux |
| :---------: | :------: | :------: | :--------: | :---: |
| Support     | Yes | Yes | [Yes](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-mcu.md) | No |

### Supported Connection buses

| Connection bus | SDIO | SPI Full-Duplex | SPI Half-Duplex | UART |
| :------------- | :--: | :-------------: | :-------------: | :--: |
| MCU host       | Yes  | Yes             | Yes             | Yes  |
<!-- common-stop -->

## Directory layout

```text
system/mcu_transport_config/
├── cp/                  ESP coprocessor firmware
└── mcu_host/main/       ESP-IDF MCU host app
     ├── main.c             bus settings, start, change, restart (the example)
     ├── events.c           link-state events (helper)
     ├── bus_monitor.c      heartbeat and recovery (optional)
     ├── wifi_sta.c         Wi-Fi station (optional)
     └── ble_nimble.c       BLE advertising (optional)
```

> [!IMPORTANT]
> **New here? Get a base example working first.** Follow **[Getting Started: MCU](../../../docs/getting-started-mcu.md)** to wire the boards, install tools, choose a transport, and confirm the host↔co-processor handshake. The steps below only add what is specific to this example.

## MCU Host Setup

### 1. Coprocessor

Set up the tools once (from the repo root):

```bash
cd /path/to/esp_hosted
./install.sh      # install.fish for the fish shell
. ./export.sh     # . ./export.fish for the fish shell
```

<!-- coprocessor-start -->
Select the transport:

```bash
cd examples/system/mcu_transport_config/cp
eh.py set-target esp32c6
eh.py menuconfig
```

```text
Component config
└── ESP-Hosted
     └── Configure coprocessor
          └── CP transport
               └── Communication bus (co-processor <== bus ==> host)
                    ├── ( ) SPI Full Duplex
                    ├── (X) SDIO                    <── default
                    ├── ( ) SPI Half Duplex
                    └── ( ) UART
```

Pre-set in `sdkconfig.defaults` (do not remove): Wi-Fi and the BLE controller, so the
host can use both.

Then flash and monitor:

```bash
eh.py -p <cp_usb_serial_port> flash monitor
```
<!-- coprocessor-stop -->

### 2. MCU Host

<!-- esp_host-start -->
Select the same transport as the co-processor:

```bash
cd examples/system/mcu_transport_config/mcu_host
eh.py set-target esp32p4
eh.py menuconfig
```

```text
Component config
└── ESP-Hosted
     └── Configure host
          └── Host transport
               └── Communication bus (co-processor <== bus ==> host)
                    ├── ( ) SPI Full Duplex
                    ├── (X) SDIO                    <── default (match the co-processor)
                    ├── ( ) SPI Half Duplex
                    └── ( ) UART
```

```text
Example Configuration
├── [*] Stage 2: bring up Wi-Fi STA        <── default on
├── (myssid)      Wi-Fi SSID
├── (mypassword)  Wi-Fi password
└── [*] Stage 3: bring up BLE advertising  <── default on
```

Pre-set in `sdkconfig.defaults` (do not remove):

```text
CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN=n   # the app starts ESP-Hosted itself
CONFIG_ESP_HOSTED_HOST_AUTO_FEAT_INIT=n              # the app starts each feature itself
```

Then flash and monitor:

```bash
eh.py -p <host_usb_serial_port> flash monitor
```
<!-- esp_host-stop -->

### 3. Verify

The host log shows each step (SDIO shown):

```text
EH stage=transport bus=sdio set clk_khz=20000 rc=0
bus applied: clk_khz=20000
EH stage=hosted linked
EH stage=transport set-while-up refused=1 rc=269
EH stage=transport bus=sdio set clk_khz=40000 rc=0
bus applied: clk_khz=40000
EH stage=transport reconfigured rc=0
EH stage=monitor started (heartbeat=10s watchdog=30000ms)
EH stage=wifi ip=...
EH stage=ble advertising rc=0
EH manual bringup complete
```

`bus applied:` is the clock the bus driver actually set.
