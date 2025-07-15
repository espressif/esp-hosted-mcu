# Changelog

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

### Features

- Added dependency on `esp_driver_gpio`

## 2.5.0

### Features

- Remove dependency on deprecated `driver` component and added necessary dependencies instead

## 2.4.3

### Features

- Added support for Wi-Fi Easy Connect (DPP)

### APIs added

- `esp_supp_remote_dpp_init`
- `esp_supp_remote_dpp_deinit`
- `esp_supp_remote_dpp_bootstrap_gen`
- `esp_supp_remote_dpp_start_listen`
- `esp_supp_remote_dpp_stop_listen`

## 2.4.2

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
