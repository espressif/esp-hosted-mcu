<!-- SPDX-License-Identifier: Apache-2.0 -->

# API exerciser

<!-- common-start -->
A generic host+CP example that exposes the native `eh_host_*` API surface as
console commands, so functional API coverage is added as **data** (a console
command + an expected line) rather than a new example per scenario.

## Supported Platforms and Transports

### Supported Coprocessors

| Coprocessor | ESP32 | ESP32-C Series | ESP32-S Series |
| :----------: | :---: | :------------: | :------------: |
| Support     | Yes   | Yes            | Yes            |

### Supported Host Devices

| Host Device | ESP32-P4 | ESP32-H2 | Other MCUs | Linux |
| :---------: | :------: | :------: | :--------: | :---: |
| Support     | Yes | Yes | [Yes](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-mcu.md) | [Yes](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-linux.md) |

### Supported Connection buses

| Connection bus | SDIO | SPI Full-Duplex | SPI Half-Duplex | UART |
| :------------- | :--: | :-------------: | :-------------: | :--: |
| Linux host     | Yes  | Yes             | No              | No   |
| MCU host       | Yes  | Yes             | Yes             | Yes  |
<!-- common-stop -->

## Directory layout

```text
system/api_exerciser/
├── cp/                  ESP coprocessor firmware
├── mcu_host/            ESP-IDF MCU host app
└── linux_802_3_host/    Linux host
     └── c_app/          native C app
```

> [!IMPORTANT]
> **New here? Get a base example working first.** Follow
> [Getting Started: MCU](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-mcu.md)
> or
> [Getting Started: Linux](https://github.com/espressif/esp-hosted/blob/master/docs/getting-started-linux.md)
> to wire the boards and confirm the host↔co-processor handshake. This example
> is normally driven by the pytest bench rather than flashed by hand.

<!-- esp_host-start -->
## The result contract

Every command prints exactly one newline-terminated line:

```
EH rc=<int> cmd=<name> [k=v ...]
```

- `rc=0` on success; a non-zero `rc` (with `err=<name>`) on failure.
- getters add fields (e.g. `wifi_get_ps` → `EH rc=0 cmd=wifi_get_ps ps=2`).
- Round-trips (set → get → compare) and negative/arg-validation checks are
  expressed entirely in the test, not in firmware.

Type `help` at the `eh>` prompt for the full command list.
<!-- esp_host-stop -->

<!-- coprocessor-start -->
## Configuration

The exerciser needs every feature whose API it offers as a command, so the
co-processor config is **pre-set in `sdkconfig.defaults`** (do not remove):

```text
CONFIG_ESP_HOSTED_CP_FEAT_WIFI=y          # Wi-Fi API surface
CONFIG_ESP_HOSTED_CP_FEAT_GPIO_EXP=y      # GPIO expander API surface
CONFIG_ESP_HOSTED_CP_FEAT_CP_EXT_COEX=y   # external-coexistence API surface
```

Dropping one of these removes the matching commands, and the tests that
drive them fail rather than skip.
<!-- coprocessor-stop -->

<!-- esp_host-start -->
## Configuration

The exerciser needs every feature whose API it offers as a command, so the
host config is **pre-set in `sdkconfig.defaults`** (do not remove):

```text
CONFIG_ESP_HOSTED_HOST_FEAT_RPC=y         # RPC control path
CONFIG_ESP_HOSTED_HOST_FEAT_RPC_EXT_V2=y  # RPC ext-v2 (required)
CONFIG_ESP_HOSTED_HOST_FEAT_SYSTEM=y
CONFIG_ESP_HOSTED_HOST_FEAT_WIFI=y
CONFIG_ESP_HOSTED_HOST_FEAT_GPIO_EXP=y
CONFIG_ESP_HOSTED_HOST_FEAT_CP_EXT_COEX=y
```

The co-processor must enable the matching CP-side features.

Dropping one of these removes the matching commands, and the tests that
drive them fail rather than skip.
<!-- esp_host-stop -->

## Run

```
# emulator (P4 host ⇄ C6 CP), via the pytest bench
python3 tools/eh.py test emu-mcu -k api_exerciser
```

## Verify

A command answers on one line and the test compares that line verbatim:

```text
eh> wifi_get_ps
EH rc=0 cmd=wifi_get_ps ps=2
```

`rc=0` is a pass. A non-zero `rc` carries `err=<name>` and the test reports
the whole line, so a failure says what the API returned rather than only
that it differed.

## Adding coverage

Add a row to `mcu_host/main/eh_api_cmd.c`'s command table (one thin
`argv → eh_host_* call → eh_out(...)` wrapper), then a pytest data file under
`tests/emu-mcu/eh_test_feat_api/`. No emulator change is needed for the
scalar/string API surface (SCENARIO_BACKLOG bucket **B2**).
