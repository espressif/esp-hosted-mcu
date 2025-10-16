# Changelog

## 2.6.0
- Added public OTA APIs for slave firmware updates
- Added host-triggered slave OTA example with support for HTTP, partition, and filesystem sources
- Support for LittleFS filesystem-based OTA updates
- Migration guide updated for 2.6.0

### APIs added

- `esp_hosted_ota_begin`
- `esp_hosted_ota_write`
- `esp_hosted_ota_end`
- `esp_hosted_ota_activate`

### APIs deprecated

- `esp_hosted_slave_ota` - Use the new [Host Performs Slave OTA Example](examples/host_performs_slave_ota/README.md) instead for more flexible OTA implementations with comprehensive documentation and multiple deployment methods

### Examples added

- `host_performs_slave_ota` - Host-triggered slave OTA example supporting HTTP URLs, partition sources and  LittleFS filesystem sources

## 2.5.12

### Features

- Add SPI (full and half duplex) and UART support for ESP32-C61
- Updated documentation on applying optimised Wi-Fi settings to sdkconfigs

### Bug Fixes

- Fixed build issues when raw throughput testing is enabled
- Fixed bug in co-processor causing SDIO to operate only in packet mode

## 2.5.11

### Bug Fixes

- Fixes to use compatible version of `idf-build-apps` and constraints during CI pipeline builds
- Renamed CI pipelines to "sanity" and "regression"
- Prefix jobs with `sanity_` or `regression_` to make their names unique
- Enabled building of ESP-Hosted examples in regression pipeline
- Various bug fixes found in the process of fixing the CI pipelines

## 2.5.10

### Features

- Version, 2.5.8 - 2.5.10:
  - Add staging branch workflow for safer component releases

## 2.5.7

### Bug Fixes

- Fixed build break when Network Split and CLI Commands are enabled on coprocessor

## 2.5.6

### Bug Fixes

- Updated co-processor and some example `idf_component.yml` files to set component dependencies based on the ESP-IDF version in use

## 2.5.5

### Bug Fixes

- Fixed build errors when using latest version of ESP-IDF
- Updated Wi-Fi Easy Connect (DPP) code to match current ESP-IDF master
- Adjusted CI pipeline

## 2.5.4

### Features

- Added building with ESP-IDF v5.3 in CI
- Added building ESP-Hosted examples in CI

### Bug Fixes

- Fixed building with ESP32-H2 as host in CI (was skipping build)

## 2.5.3

### Bug Fixes

- Fix the ESP-IDF CI

## 2.5.2

### Features

- Add support to get and set the BT Controller Mac Address
  - To support set BT Controller Mac Address, BT Controller is now disabled by default on the co-processor, and host must enable the BT Controller. See [Initializing the Bluetooth Controller](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/bluetooth_design.md#31-initializing-the-bluetooth-controller) for details
- Updated all ESP-Hosted BT related examples to account for new BT Controller behaviour

### APIs added

- `esp_hosted_bt_controller_init`
- `esp_hosted_bt_controller_deinit`
- `esp_hosted_bt_controller_enable`
- `esp_hosted_bt_controller_disable`
- `esp_hosted_iface_mac_addr_set`
- `esp_hosted_iface_mac_addr_get`
- `esp_hosted_iface_mac_addr_len_get`

## 2.5.1

### Bug Fixes

- Added dependency on `esp_driver_gpio`

## 2.5.0

### Bug Fixes

- Remove dependency on deprecated `driver` component and added necessary dependencies instead

## 2.4.3

### Features

- Add support for Wi-Fi Easy Connect (DPP)
  - [Espressif documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_dpp.html) on Wi-Fi Easy Connect (DPP)
  - [ESP-Hosted Enrollee Example](https://github.com/espressif/esp-hosted-mcu/tree/main/examples/host_wifi_easy_connect_dpp_enrollee) using DPP to securely onboard a ESP32P4 with C6 board to a network with the help of a QR code and an Android 10+ device

### APIs added

- `esp_supp_dpp_init`
- `esp_supp_dpp_deinit`
- `esp_supp_dpp_bootstrap_gen`
- `esp_supp_dpp_start_listen`
- `esp_supp_dpp_stop_listen`

## 2.4.2

### Bug Fixes

- Fix ignored lwip hook header in slave example

## 2.4.1

### Bug Fixes

- Reduced ESP32 bootloader size

## 2.4.0

### Features

- Added support for Wi-Fi Enterprise

### APIs added

- `esp_wifi_sta_enterprise_enable`
- `esp_wifi_sta_enterprise_disable`
- `esp_eap_client_set_identity`
- `esp_eap_client_clear_identity`
- `esp_eap_client_set_username`
- `esp_eap_client_clear_username`
- `esp_eap_client_set_password`
- `esp_eap_client_clear_password`
- `esp_eap_client_set_new_password`
- `esp_eap_client_clear_new_password`
- `esp_eap_client_set_ca_cert`
- `esp_eap_client_clear_ca_cert`
- `esp_eap_client_set_certificate_and_key`
- `esp_eap_client_clear_certificate_and_key`
- `esp_eap_client_set_disable_time_check`
- `esp_eap_client_get_disable_time_check`
- `esp_eap_client_set_ttls_phase2_method`
- `esp_eap_client_set_suiteb_192bit_certification`
- `esp_eap_client_set_pac_file`
- `esp_eap_client_set_fast_params`
- `esp_eap_client_use_default_cert_bundle`
- `esp_wifi_set_okc_support`
- `esp_eap_client_set_domain_name`
- `esp_eap_client_set_eap_methods`

## 2.3.3

### Features

- Added SDIO support for ESP32-C61

## 2.3.2

### Features

- Add host example to showcase transport config before `esp_hosted_init()`

## 2.3.1

### Bug Fixes

- Fixed a build break caused by refactoring

## 2.3.0

### Features

- Refactored common and port specific code
