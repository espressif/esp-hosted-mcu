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

CI builds use preset sdkconfig files in `slave/sdkconfig.ci.*`:
```bash
cd slave
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci.spi" set-target esp32c6
idf.py build
```

Available CI configs: `sdio`, `spi`, `spi_hd`, `uart`, `dpp`, `wifi_enterprise`, `all_features`.

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

To build an example against local `esp_hosted` changes (CI-style):
```bash
cd examples/<example_name>
mkdir -p components
ln -sf <path_to_esp_hosted_repo> components/esp_hosted
idf.py set-target esp32p4
idf.py build
```

### Pre-commit Checks

```bash
pre-commit run --all-files
```

Individual checks can also be run directly:
```bash
python tools/check_fw_versions.py          # Version sync check
python tools/check_rpc_calls.py            # RPC consistency check
python tools/check_changelog.py            # Changelog check
python tools/check_weak_functions.py --file host/api/src/esp_wifi_weak.c
```

Pre-commit hooks verify:
- Version sync across `idf_component.yml`, `host/esp_hosted_host_fw_ver.h`, and `slave/main/esp_hosted_coprocessor_fw_ver.h`
- RPC consistency between proto, host wrappers, and slave handlers
- Weak function coverage in `host/api/src/esp_wifi_weak.c`
- Changelog updates
- Copyright headers

### Tests

There are no traditional unit tests in this repository. Verification is done via:
- Pre-commit checks (see above)
- CI build matrices across IDF versions, targets, and transports (see `.gitlab/ci/`)
- Raw throughput testing for transport validation (enable `CONFIG_ESP_HOSTED_RAW_TP` in menuconfig)

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

### Async Event Path

Wi-Fi and Bluetooth events originate on the co-processor and flow to the host:

```
Co-processor ESP-IDF event
  → Slave control (slave/main/slave_control.c)
  → Protobuf event serialization
  → Slave transport
  → Host transport
  → RPC core deserializes event
  → Injected into host ESP-IDF event loop via esp_event_post()
  → App receives standard Wi-Fi/Bluetooth events
```

Events use the same protobuf schema but are sent unsolicited. The host registers standard ESP-IDF event handlers (`WIFI_EVENT`, `IP_EVENT`, etc.) and receives events transparently.

### Data Path (Network/Bluetooth)

Raw network frames and Bluetooth HCI packets bypass protobuf. They are encapsulated with a lightweight hosted header (defined in `common/transport/esp_hosted_header.h`) and passed directly over the transport driver.

- **Network interfaces**: `ESP_STA_IF` (1), `ESP_AP_IF` (2)
- **Control interface**: `ESP_SERIAL_IF` (3)
- **Bluetooth interface**: `ESP_HCI_IF` (4)
- **Private interface**: `ESP_PRIV_IF` (5)
- **Test interface**: `ESP_TEST_IF` (6) — used for raw throughput testing

The hosted header is prepended to every frame. It contains `if_type`, `if_num`, payload length, offset, checksum, and sequence number. Data packets do not need endianness conversion; only the header is parsed.

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

Bluetooth HCI can travel over the shared transport (Hosted HCI / multiplexed) or a dedicated UART (Standard HCI). These are mutually exclusive.

- **Hosted HCI**: Standard HCI encapsulated with the ESP-Hosted header, multiplexed on SPI/SDIO/UART. No extra GPIOs needed. See `host/drivers/bt/vhci_drv.c`.
- **Standard HCI**: Dedicated UART transport for Bluetooth only. Requires separate UART pins. Used when Bluetooth transparency or portability to non-ESP co-processors is needed.
- **HCI stub**: `host/drivers/bt/hci_stub_drv.c` (when BT disabled)
- **Slave BT**: `slave/main/slave_bt.c`

Stack agnostic: supports both NimBLE and BlueDroid on the host. See `docs/bluetooth_design.md`.

### Port Layer (Non-ESP Hosts)

The host code is designed to port to non-ESP MCUs. ESP-IDF-specific functionality is isolated behind abstractions:

- **OS abstraction**: `host/port/esp/freertos/` — FreeRTOS/ESP-IDF port
- **OS abstraction header**: `host/esp_hosted_os_abstraction.h`
- **Config header**: `host/esp_hosted_host_config.h`

To port to a new host MCU, implement the OS abstraction layer and provide a custom `port_esp_hosted_host_config.h`.

## Weak Function Mechanism

The `esp_wifi_remote` component provides empty weak definitions of `esp_wifi_*` APIs. ESP-Hosted provides the real implementations in `host/api/src/esp_wifi_weak.c`. When `esp_wifi_remote` lacks a required API, the weak fallback in ESP-Hosted bridges the call to `esp_wifi_remote_*`, which then forwards into the ESP-Hosted RPC path.

The `tools/check_weak_functions.py` tool ensures every `esp_wifi_*` API implemented by ESP-Hosted has a corresponding weak definition.

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

Feature-specific docs:
- Network split: `docs/feature_network_split.md`
- Host power save: `docs/feature_host_power_save.md`
- GPIO expander: `docs/gpio_expander.md`

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
