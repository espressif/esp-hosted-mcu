# Custom BT Stack — raw HCI over Hosted (`bluetooth/esp_hosted_custom/hci_demo`)

<!-- tags: bluetooth, ble, hci, custom-stack, bring-your-own-stack, hosted-hci -->

<!-- common-start -->
A **bring-your-own BT stack** example. No NimBLE, no Bluedroid — the host binds
its **own raw-HCI handler** to the ESP-Hosted HCI byte-pipe through the **custom**
path, so you can drive any BT host stack you like over the co-processor's
controller.

It brings the **controller** up on the ESP-Hosted **co-processor**, binds a custom
`rx`/`tx` pair with one call — `esp_hosted_bt_host_stack_setup()` using
`ESP_HOSTED_BT_HOST_STACK_CONFIG_CUSTOM(rx, ctx)` — then sends an **HCI Reset** and
prints the controller's **Command-Complete** received on `rx`. That round-trip
(`tx` → CP controller → `rx`) is the proof the custom two-wire path works. Wire a
real stack's transport to the same `rx`/`tx` and you have hosted BT with your own
stack. See [Porting a BT stack to
ESP-Hosted](https://github.com/espressif/esp-hosted/blob/master/docs/design/bluetooth.md#porting-a-bt-stack-to-esp-hosted).

## Supported Platforms and Transports

### Supported Coprocessors (BT controller)

| Coprocessor | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-S3 | ESP32-S2 |
| :---------- | :---: | :------: | :------: | :------: | :------: | :-------: | :------: | :------: | :------: |
| BLE support | Yes   | Yes      | Yes      | Yes      | Yes      | Yes       | Yes      | Yes      | No (no BLE) |

### Supported Host Devices

| Host Device | ESP32-P4 | ESP32-H2 | Other MCUs |
| :---------- | :------: | :------: | :--------: |
| Support     | Yes      | Yes      | [Yes](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-mcu.md) |

### Supported HCI transports

| HCI over hosted bus (VHCI) | SDIO | SPI Full-Duplex | SPI Half-Duplex | UART |
| :------------------------- | :--: | :-------------: | :-------------: | :--: |
| MCU host                   | Yes  | Yes             | Yes             | Yes  |
<!-- common-stop -->

## Directory layout

```text
bluetooth/esp_hosted_custom/hci_demo/
├── cp/         # co-processor: BT controller-only over hosted HCI (stack-agnostic)
└── mcu_host/   # host: custom raw-HCI handler (no NimBLE / Bluedroid)
```

> [!IMPORTANT]
> **New here? Get a base example working first.** Follow
> [Getting Started: MCU](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-mcu.md)
> to wire the boards, install tools, choose a transport, and confirm the
> host↔co-processor handshake. Host and co-processor must select the **same**
> transport.

## Co-processor (BT controller)

<!-- coprocessor-start -->
The co-processor runs the BT controller only — no host stack — and exposes HCI
over the hosted bus. Select the transport (must match the host):

```bash
cd examples/bluetooth/esp_hosted_custom/hci_demo/cp
eh.py set-target esp32c6
eh.py menuconfig
```

CP dependency config is **pre-set in `sdkconfig.defaults`** (do not remove):

```text
CONFIG_ESP_HOSTED_CP_FEAT_BT=y            # BT feature on the co-processor
CONFIG_ESP_HOSTED_CP_BT_ENABLED=y
CONFIG_ESP_HOSTED_CP_FEAT_BT_HCI_VHCI=y   # HCI over the hosted bus, not a UART
CONFIG_BT_ENABLED=y
CONFIG_BT_CONTROLLER_ONLY=y               # controller only — the host owns the stack
CONFIG_ESP_HOSTED_CP_FEAT_WIFI=n          # not needed here
```

Then flash and monitor:

```bash
eh.py -p <cp_usb_serial_port> flash monitor
```
<!-- coprocessor-stop -->

## MCU host (custom stack)

<!-- esp_host-start -->
Select the transport (must match the co-processor):

```bash
cd examples/bluetooth/esp_hosted_custom/hci_demo/mcu_host
eh.py set-target esp32p4
eh.py menuconfig
```

Host dependency config is **pre-set in `sdkconfig.defaults`** (do not remove).
The host enables the BT feature but **no** IDF BT host stack — that absence is
what selects the custom path:

```text
CONFIG_ESP_HOSTED_HOST_FEAT_BT=y      # HCI byte-pipe + CP controller lifecycle
# (no CONFIG_BT_NIMBLE_ENABLED / CONFIG_BT_BLUEDROID_ENABLED)
```

Then flash and monitor:

```bash
eh.py -p <host_usb_serial_port> flash monitor
```

### Verify

The host sends an HCI Reset and prints the controller's Command Complete as it
arrives on the custom `rx` callback. That round-trip (`tx` → CP controller →
`rx`) is the proof the two-wire custom path works:

```text
custom rx: HCI Reset Command Complete, status=0x00
```

A `status` other than `0x00`, or no line at all, means the controller never
answered — check that both sides selected the same transport.
<!-- esp_host-stop -->
