# AGENTS.md

Build, test, and verification guidance for automated agents working in this repo. See also `CLAUDE.md` for architecture details.

## Build commands

This is an **ESP-IDF component** (`idf_component.yml`). Use `idf.py`, never raw `cmake` or `make`.

### Co-processor (slave) firmware

```bash
cd slave
idf.py set-target <TARGET>                          # e.g. esp32c6
idf.py menuconfig                                   # transport under "ESP-Hosted config"
idf.py build
```

CI-style build with preset config:
```bash
cd slave
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci.spi" set-target esp32c6
idf.py build
```
Presets: `sdio`, `spi`, `spi_hd`, `uart`, `dpp`, `wifi_enterprise`, `all_features`.

### Host examples

Build against the **registered component** (default):
```bash
cd examples/<name>
idf.py set-target <TARGET>
idf.py add-dependency "espressif/esp_wifi_remote"
idf.py build
```

Build against **local repo changes** (CI / dev pattern):
```bash
cd examples/<name>
mkdir -p components
ln -s <path_to_esp_hosted_mcu_repo> components/esp_hosted
idf.py set-target esp32p4
idf.py build
```

**There are no unit tests.** The verification strategy is:

1. **Pre-commit hooks** (mandatory before commit):
   ```bash
   pre-commit run --all-files
   ```
   Individual checks:
   ```bash
   python tools/check_fw_versions.py        # version sync
   python tools/check_rpc_calls.py          # RPC consistency
   python tools/check_changelog.py          # changelog
   python tools/check_weak_functions.py --file host/api/src/esp_wifi_weak.c
   ```

2. **CI build matrix** — sanity + regression builds across IDF versions, targets, transports (see `.gitlab/ci/`, `.gitlab-ci.yml`).

## Version sync (3 files — edit ALL)

When bumping version, update **all three** — the pre-commit hook enforces this:
- `idf_component.yml` (line 1)
- `host/api/include/esp_hosted_host_fw_ver.h`
- `slave/main/esp_hosted_coprocessor_fw_ver.h`

## Adding an RPC

1. Add message to `common/proto/esp_hosted_rpc.proto`
2. Regenerate: `cd common/proto && protoc-c esp_hosted_rpc.proto --c_out=.`
3. Add host wrapper in `host/drivers/rpc/wrap/rpc_wrap.c`
4. Add slave handler in `slave/main/slave_control.c`
5. Document in `docs/implemented_rpcs.md`
6. Run `pre-commit run --all-files`

## Quirks & gotchas

- **Slave and every example are separate ESP-IDF projects** — each has its own `CMakeLists.txt` calling `project()`. You cannot build from the repo root.
- **`common/protobuf-c` is a git submodule** — clone with `--recurse-submodules` or run `git submodule update --init`.
- **The component uses `WHOLE_ARCHIVE`** (`idf_component_set_property WHOLE_ARCHIVE TRUE` in root `CMakeLists.txt:145`) to prevent the linker from dropping weak function overrides.
- **`esp_wifi_remote` is a separate component** (from registry) that provides empty weak `esp_wifi_*` stubs. ESP-Hosted provides the real implementations in `host/api/src/esp_wifi_weak.c`. The `check_weak_functions.py` tool ensures coverage.
- **The `-Wl,--wrap=esp_wifi_init`** linker flag in `slave/CMakeLists.txt:6` is how the slave intercepts `esp_wifi_init`.
- **ESP-IDF >= 5.3 required.**
- **Transport selection is compile-time via Kconfig**, not runtime. The CMakeLists conditionally includes only one transport driver.

## Key file map

| Path | Role |
|---|---|
| `host/api/src/esp_wifi_weak.c` | Real weak API implementations (checked by `check_weak_functions.py`) |
| `host/drivers/rpc/wrap/rpc_wrap.c` | RPC → Wi-Fi API wrappers (host side) |
| `slave/main/slave_control.c` | RPC handler dispatch (slave side) |
| `common/proto/esp_hosted_rpc.proto` | Protobuf RPC schema |
| `common/proto/esp_hosted_rpc.pb-c.c/h` | Generated protobuf-c code (do not hand-edit) |
| `common/transport/esp_hosted_header.h` | Transport frame header definition |
| `host/drivers/transport/` | SPI, SDIO, UART transport drivers |
| `slave/main/*_slave_api.c` | Slave-side transport drivers |
| `idf_component.yml` | Component manifest + version |
| `Kconfig` | All Kconfig options (1500+ lines, shared host/slave) |
| `slave/sdkconfig.ci.*` | CI preset sdkconfigs per transport |
