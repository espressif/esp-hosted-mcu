# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-Hosted-MCU is an ESP-IDF component that enables using Espressif chips as Wi-Fi/Bluetooth co-processors for host MCUs via SPI, SDIO, or UART. The host application uses standard ESP-IDF Wi-Fi APIs (`esp_wifi_*`) through the `esp_wifi_remote` component, which forwards calls over RPC to the co-processor.

- **Host**: `host/` — driver running on the host MCU (any ESP or non-ESP MCU)
- **Slave/Co-processor**: `slave/` — firmware running on the ESP providing Wi-Fi/BT
- **Common**: `common/` — protobuf definitions, mempool, transport headers, utilities

## Build System

This is an ESP-IDF CMake project. ESP-IDF >= 5.3 is required.

### Build Co-processor (Slave)

```bash
cd slave
idf.py set-target <TARGET>     # e.g. esp32c6, esp32
idf.py menuconfig              # Configure transport under Component config > ESP-Hosted
idf.py build
idf.py -p <PORT> flash monitor
```

### Build Host Example

Host examples are in `examples/`. They consume this component via the ESP Component Registry (`espressif/esp_hosted`) or from the local repo.

```bash
cd examples/<example_name>
idf.py set-target <TARGET>     # e.g. esp32p4
idf.py add-dependency "espressif/esp_wifi_remote"
idf.py add-dependency "espressif/esp_hosted"   # if using registry
idf.py menuconfig
idf.py build
idf.py -p <PORT> flash monitor
```

### Pre-commit Checks

```bash
pre-commit run --all-files
```

Pre-commit hooks verify:
- Version sync across `idf_component.yml`, `host/esp_hosted_host_fw_ver.h`, and `slave/main/esp_hosted_coprocessor_fw_ver.h`
- RPC consistency between proto, host wrappers, and slave handlers
- Weak function coverage in `host/api/src/esp_wifi_weak.c`
- Changelog updates
- Copyright headers

## Architecture

### Control Path (RPC)

Wi-Fi API calls flow as follows:

```
App (esp_wifi_init() etc.)
  → esp_wifi_remote (weak API forwarding)
  → ESP-Hosted Host API (host/api/)
  → RPC wrapper (host/drivers/rpc/wrap/rpc_wrap.c)
  → RPC core (host/drivers/rpc/core/) — protobuf serialization
  → Transport driver (host/drivers/transport/)
  → Slave transport (slave/main/*_slave_api.c)
  → Slave control (slave/main/slave_control.c) — protobuf deserialization
  → ESP-IDF Wi-Fi API on co-processor
```

- **RPC definitions**: `common/proto/esp_hosted_rpc.proto`
- **Generated code**: `common/proto/esp_hosted_rpc.pb-c.c/h` (protobuf-c)
- **Host wrappers**: `host/drivers/rpc/wrap/rpc_wrap.c`
- **Slave handlers**: `slave/main/slave_control.c`
- **Implemented RPCs**: documented in `docs/implemented_rpcs.md`

### Data Path (Network/Bluetooth)

Raw network frames and Bluetooth HCI packets bypass protobuf. They are encapsulated with a lightweight hosted header (defined in `common/transport/`) and passed directly over the transport driver.

- **Network interfaces**: `ESP_STA_IF` (1), `ESP_AP_IF` (2)
- **Control interface**: `ESP_SERIAL_IF` (3)
- **Bluetooth interface**: `ESP_HCI_IF` (4)
- **Private interface**: `ESP_PRIV_IF` (5)

### Transport Layer

Host transport drivers are selected at build time via Kconfig:

| Kconfig Option | Driver File |
|---|---|
| `CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE` | `host/drivers/transport/spi/spi_drv.c` |
| `CONFIG_ESP_HOSTED_SPI_HD_HOST_INTERFACE` | `host/drivers/transport/spi_hd/spi_hd_drv.c` |
| `CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE` | `host/drivers/transport/sdio/sdio_drv.c` |
| `CONFIG_ESP_HOSTED_UART_HOST_INTERFACE` | `host/drivers/transport/uart/uart_drv.c` |

Corresponding slave transports are in `slave/main/spi_slave_api.c`, `sdio_slave_api.c`, `spi_hd_slave_api.c`, `uart_slave_api.c`.

### Bluetooth

Bluetooth HCI can travel over the shared transport (Hosted HCI / multiplexed) or a dedicated UART (Standard HCI).

- **VHCI driver**: `host/drivers/bt/vhci_drv.c` (shared transport)
- **HCI stub**: `host/drivers/bt/hci_stub_drv.c` (when BT disabled)
- **Slave BT**: `slave/main/slave_bt.c`

Stack agnostic: supports both NimBLE and BlueDroid on the host. See `docs/bluetooth_design.md`.

## Adding a New RPC

1. Add the RPC message to `common/proto/esp_hosted_rpc.proto`
2. Regenerate C bindings:
   ```bash
   cd common/proto
   protoc-c esp_hosted_rpc.proto --c_out=.
   ```
3. Add host wrapper in `host/drivers/rpc/wrap/rpc_wrap.c`
4. Add slave handler in `slave/main/slave_control.c`
5. Document in `docs/implemented_rpcs.md`
6. Run `pre-commit run --all-files` to validate consistency

## Key Configuration (Kconfig)

The top-level `Kconfig` defines options under `Component config > ESP-Hosted`:

- **Transport**: SDIO / SPI / SPI-HD / UART
- **Co-processor target**: ESP32, ESP32-C2/C3/C5/C6/C61, ESP32-S2/S3, ESP32-H2/H4
- **Bluetooth mode**: NimBLE VHCI, BlueDroid VHCI, UART HCI, or disabled
- **Features**: Network split, host power save, GPIO expander, peer data transfer

## Version Management

Versions are synchronized across three files by the pre-commit hook (`tools/check_fw_versions.py`):

- `idf_component.yml`
- `host/api/include/esp_hosted_host_fw_ver.h`
- `slave/main/esp_hosted_coprocessor_fw_ver.h`

Do not manually edit only one of these.

## Important Files and Directories

| Path | Purpose |
|---|---|
| `host/api/include/` | Public headers (`esp_hosted.h`, `esp_hosted_ota.h`, etc.) |
| `host/drivers/rpc/` | RPC core, slave interface, wrappers |
| `host/drivers/transport/` | Transport abstraction + SPI/SDIO/UART drivers |
| `host/port/esp/freertos/` | ESP-IDF FreeRTOS port layer |
| `slave/main/` | Co-processor main application, control, Wi-Fi, BT handlers |
| `common/proto/` | Protobuf definitions and generated code |
| `common/mempool/` | Custom memory pool allocator |
| `examples/` | 19 example host applications |
| `docs/` | Design docs, transport setup guides, troubleshooting |

## Dependencies

- ESP-IDF >= 5.3
- `espressif/esp_wifi_remote` (component registry)
- `protobuf-c` (submodule at `common/protobuf-c`)

## CI/CD

- **GitLab CI** (primary): `.gitlab/ci/` — sanity builds, regression builds across IDF versions and targets
- **GitHub Actions**: `.github/workflows/` — component registry upload, Jira sync
