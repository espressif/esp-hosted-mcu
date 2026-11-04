# Classic BT Discovery — Bluedroid Host over Standard HCI UART (`bluetooth/bluedroid_uart/bluedroid_host_only_uart`)

<!-- tags: bluetooth, classic-bt, bredr, bluedroid, standard-hci, uart -->

<!-- common-start -->
Classic Bluetooth (BR/EDR) device **and** service discovery. The Bluedroid host
stack runs on the **host** MCU with its local controller disabled; it reaches the
Bluetooth controller on the co-processor over a **dedicated physical UART in H4
mode** (not the ESP-Hosted VHCI bridge). The host's own `uart_driver.c` is bound
to Bluedroid via `esp_bluedroid_attach_hci_driver()`. This is the Bluedroid
counterpart of `bluetooth/nimble_uart/bleprph_host_only_uart` (NimBLE / BLE).

## Supported Platforms and Transports

### Supported Coprocessors (BT controller, UART-H4 mode)

Classic BR/EDR requires an ESP32 or ESP32-S31 controller — the BLE-only chips cannot serve
the classic profile this demo discovers.

| Coprocessor | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-S3 | ESP32-S31 |
| :---------- | :---: | :------: | :------: | :------: | :------: | :------: | :-------: |
| BR/EDR (classic) | Yes | No (BLE only) | No (BLE only) | No (BLE only) | No (BLE only) | No (BLE only) | Yes ¹ |

¹ ESP32-S31 needs ESP-IDF with commit `64018e19824` (v6.2 or later). With ESP-IDF v6.1, the ESP32-S31 controller drops host commands when Bluedroid sends them quickly.

### Supported Host Devices

| Host device | ESP32-P4 | ESP32-H2 | Other MCUs |
| :---------- | :------: | :------: | :--------: |
| Support     | Yes      | Yes      | [Yes](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-mcu.md) |

### HCI transport

| HCI link           | Dedicated UART (H4) |
| :----------------- | :-----------------: |
| Host ↔ controller  | Yes — RTS/CTS flow control recommended; both ends must match |

Bluetooth HCI here runs over its own UART, independent of the ESP-Hosted bus a
host may separately use for Wi-Fi.
<!-- common-stop -->

## Directory layout

```text
bluetooth/bluedroid_uart/bluedroid_host_only_uart/
├── cp/          BT-controller co-processor firmware (HCI on a dedicated UART, H4)
└── mcu_host/    ESP-IDF Bluedroid host app (classic BT discovery over that UART)
```

> [!IMPORTANT]
> **New here? Get a base example working first.** Follow
> [Getting Started: MCU](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-mcu.md)
> to wire the boards, install tools, and confirm the host↔co-processor link. The
> steps below only add what is specific to this example. The BT HCI UART is a
> **separate** link from any hosted (SDIO/SPI) bus and must be wired directly,
> host UART ↔ co-processor UART. Connect RTS/CTS if flow control is on.

## Co-processor (BT controller, UART-H4 mode)

<!-- coprocessor-start -->
The co-processor runs the BT controller with HCI exposed on a physical UART (H4),
not on the ESP-Hosted bus. This example discovers **classic BR/EDR** devices.
Build the co-processor on an **ESP32** or **ESP32-S31**. These are the only
Espressif controllers with the classic profile. Keep BR/EDR enabled in the
controller:

```bash
cd examples/bluetooth/bluedroid_uart/bluedroid_host_only_uart/cp
eh.py set-target esp32              # or: eh.py --preview set-target esp32s31
eh.py menuconfig
```

ESP32 co-processor:

```text
Component config
└── Bluetooth
     └── Controller Options
          ├── Bluetooth controller mode
          │    └── (X) BR/EDR + BLE           <── classic profile must be present
          └── HCI mode
               ├── ( ) VHCI
               └── (X) UART                    <── HCI over a dedicated UART (H4)
                    ├── HCI UART Tx pin        ← wire to host UART Rx
                    ├── HCI UART Rx pin        ← wire to host UART Tx
                    └── [*] HCI UART flow control (RTS/CTS)   <── default on
```

ESP32-S31 co-processor:

```text
Component config
└── Bluetooth
     └── Bluetooth controller HCI
          ├── HCI mode
          │    └── (X) UART(H4)                <── set by sdkconfig.defaults.esp32s31
          ├── HCI uart Tx gpio / Rx gpio       ← set the pins for your board
          └── [ ] HCI uart Hardware Flow ctrl  <── default off (CONFIG_BT_CTRL_HCI_UART_FLOWCTRL)
```

> [!IMPORTANT]
> The host turns flow control on by default. The ESP32-S31 controller turns it
> off by default. Make the two ends match. Either set
> `CONFIG_BT_CTRL_HCI_UART_FLOWCTRL=y` on the co-processor and set its RTS and
> CTS pins, or clear **UART hardware flow control (RTS/CTS)** on the host.

The CP dependency config is pre-set in `sdkconfig.defaults` (do not remove):

```text
CONFIG_ESP_HOSTED_CP_FEAT_BT=y            # ESP-Hosted BT controller feature
CONFIG_ESP_HOSTED_CP_BT_ENABLED=y         # required by ESP_HOSTED_CP_FEAT_BT
CONFIG_ESP_HOSTED_CP_FEAT_BT_HCI_UART=y   # BT HCI on a dedicated UART (H4), NOT the hosted bus
CONFIG_BT_ENABLED=y                       # BT controller stack on the CP
CONFIG_BT_CONTROLLER_ONLY=y               # controller-only profile
CONFIG_ESP_HOSTED_CP_FEAT_WIFI=n          # Wi-Fi off — BT controller-only CP
```

Then flash and monitor:

```bash
eh.py -p <cp_usb_serial_port> flash monitor
```
<!-- coprocessor-stop -->

## MCU host (Bluedroid, application UART transport)

<!-- esp_host-start -->
The host runs the Bluedroid stack with its **local controller disabled** and
drives the HCI UART itself. Set the target and the example options:

```bash
cd examples/bluetooth/bluedroid_uart/bluedroid_host_only_uart/mcu_host
eh.py set-target esp32p4
eh.py menuconfig
```

This example adds these options under **Example Configuration**:

```text
Example Configuration
├── (921600) UART Baudrate for HCI          ← range 115200–921600; must match the controller
├── (5) UART Tx Pin
├── (18) UART Rx Pin
├── [*] UART hardware flow control (RTS/CTS) ← must match the controller
├── (19) UART Rts Pin                        ← only when flow control is on
└── (23) UART Cts Pin                        ← only when flow control is on
```

The host dependency config is pre-set in `sdkconfig.defaults` (do not remove):

```text
CONFIG_BT_ENABLED=y               # BT host stack on
CONFIG_BT_CONTROLLER_DISABLED=y   # no local controller — the CP supplies it over UART
CONFIG_BT_BLUEDROID_ENABLED=y     # Bluedroid host stack
CONFIG_BT_CLASSIC_ENABLED=y       # classic BR/EDR profile (discovery)
```

The host binds its own `uart_driver.c` to Bluedroid at startup:

```c
hci_uart_open();
esp_bluedroid_hci_driver_operations_t ops = {
    .send                  = hci_uart_send,
    .check_send_available  = hci_check_send_available,
    .register_host_callback = hci_register_host_callback,
};
esp_bluedroid_attach_hci_driver(&ops);
```

Then flash and monitor:

```bash
eh.py -p <host_usb_serial_port> flash monitor
```

### Wiring

- Host UART **TX → controller UART RX**, host UART **RX → controller UART TX**.
- If flow control is on, cross-wire **RTS → CTS** and **CTS → RTS**. Flow control
  is recommended at 921600 baud. Set it the same on both ends.
- Common ground between the two boards.
- The HCI baudrate (host `Example Configuration`) must match the controller's
  HCI UART baudrate.

### Verify

- On boot the host opens the HCI UART, attaches it to Bluedroid, then starts a
  classic **inquiry**; discovered devices (address, class, RSSI, name) are logged,
  followed by SDP **service discovery** on a found device.
- If nothing is discovered, examine the UART wiring (including RTS/CTS), the
  baudrates, and the flow control setting on both ends. Make sure that the
  co-processor is an ESP32 or ESP32-S31 with BR/EDR enabled.
<!-- esp_host-stop -->
