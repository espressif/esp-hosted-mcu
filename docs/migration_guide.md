This migration guide documents key changes in ESP-Hosted that users must be aware of when migrating from older versions.

#### Index
1. [2.5.2 - Bluetooth Controller on Co-Processor Disabled by Default](#coloryellow-text252---bluetooth-controller-on-co-processor-disabled-by-default)
2. [2.6.0 - ESP-Hosted Slave OTA](#coloryellow-text260---esp-hosted-slave-ota)
3. [2.11.0 - ESP-Hosted Host Driver](#coloryellow-text2110---esp-hosted-host-driver)
4. [2.12.4 - Custom Msg Callback - User Ptr](#coloryellow-text2124---custom-msg-callback---user-ptr)
5. [Unreleased - Pre-allocation of SDIO Rx buffers on host](#coloryellow-textunreleased---pre-allocation-of-sdio-rx-buffers-on-host)

# $${\color{yellow} \text{Unreleased - Pre-allocation of SDIO Rx buffers on host}}$$

Previously, when using SDIO Streaming mode, the Host would allocate Rx buffers big enough to hold the received data during run-time. This can lead to failed allocations if heap space is tight on the host.

Now, ESP-Hosted pre-allocates the Rx buffer during init (based on the number of Tx buffers configured on the co-processor), so there should be no read failures due to failed allocations during run-time. Double buffering can also be disabled to reduce heap memory required.

Summary of change, with co-processor configured with 20 Tx buffers (the default):

**Before**: buffers grow on demand, growing to a maximum of (20 * 1536) * 2 = 60 KB. May encounter issues if memory cannot be allocated.

**After**: 60 KB with double buffering, 30 KB without double buffering pre-allocated. No further memory allocations required.

> [!NOTE]
> ESP-Hosted will still do run-time allocation of the Rx buffer if the number of Tx buffers configured on the co-processor is not known. This can happen if the co-processor is an older version that does not send that information. A warning will be emitted if this happens. In this condition, ESP-Hosted memory allocation attempt can still fail, resulting in a stuck system.

To reduce memory usage:

1. Reduce the number of SDIO Tx buffers on the co-processor
2. Disable double buffering (can lower peak performance). See [Reducing heap usage further](sdio.md#reducing-heap-usage-further-and-preventing-allocation-issues-on-the-host) for more information.
3. Switch to Packet Mode. This uses the host Rx Queue Mempool directly and does not allocate additional memory.

# $${\color{yellow} \text{2.12.4 - Custom Msg Callback - User Ptr}}$$

## Migration needed from versions


| Firmware | Version    | Migration required |
| -------- | ---------- | ------------------ |
| Host     | < 2.12.4  | ✅                  |
| Slave    | < 2.12.4  | ✅                  |

## Reason for change

1. `esp_hosted_register_custom_callback()` now supports a user-provided pointer to be passed back on every callback invocation.
2. This allows external code to maintain per-callback context without global variables.

### Old API

```c
esp_err_t esp_hosted_register_custom_callback(
    uint32_t msg_id,
    void (*callback)(uint32_t msg_id, const uint8_t *data, size_t data_len));

esp_err_t esp_hosted_send_custom_data(uint32_t msg_id, const uint8_t *data, size_t data_len);
```

### New API

```c
esp_err_t esp_hosted_register_custom_callback(uint32_t msg_id_exp,
    void (*callback)(uint32_t msg_id_recvd, const uint8_t *data_recvd, size_t data_len_recvd, void *local_context), // <-- Changed
    void *local_context); // <-- Extra argument

esp_err_t esp_hosted_send_custom_data(uint32_t msg_id_to_send, const uint8_t *data_to_send, size_t data_len_to_send); // no logical change
```

*Arguments:*

* `msg_id` – message ID to register
* :zap: `callback` – function pointer to handle the message (adds void* as last arg)
* :zap: `user` – user-provided pointer returned on every callback invocation

*Returns:* `ESP_OK` on success, or an error code on failure.


#  $${\color{yellow} \text{2.11.0 - ESP-Hosted Host Driver}}$$

## Migration needed from versions

| Host version | wifi-remote version |
| ------------ | ------------------- |
| < 2.11.0     | < 1.3.1             |

1. A double-free memory error can occur in some situations when ESP-Hosted Host receives network data and passes it to the netif `rx()` function (registered by netif via the wifi-remote component) for processing.

2. This error is resolved in ESP-Hosted v2.11.1. It must be used with wifi-remote v1.3.1 or greater to prevent a memory leak condition during netif initialization.

#  $${\color{yellow} \text{2.6.0 - ESP-Hosted Slave OTA}}$$


## Migration needed from versions

| Slave version | Host version |
| ------------- | ------------ |
| > 2.5.X      | > 2.5.X     |

## Reason for change

1. The existing `esp_hosted_slave_ota()` API was restrictive, supporting only HTTP-based OTA updates.
   The OTA APIs are now exposed so developers can implement their own OTA mechanisms.
2. The port layer previously contained OTA logic, which forced inclusion of the HTTP client in the host codebase even when not required.

## Changes required on host

If you are migrating from the old `esp_hosted_slave_ota()` function, update your code as follows.

### Old API (deprecated)

```c
#include "esp_hosted.h"

const char *image_url = "http://example.com/network_adapter.bin";
esp_err_t ret = esp_hosted_slave_ota(image_url);
if (ret == ESP_OK) {
    printf("OTA update failed[%d]\n", ret);
}
```

## New APIs

The slave OTA process is now performed using the following APIs.

### `esp_hosted_slave_ota_begin()`

```c
esp_err_t esp_hosted_slave_ota_begin(void);
```

Initializes the OTA process on the slave.

* Arguments: None
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Prepares the slave for firmware reception
  * Allocates OTA buffers
  * Sets up the OTA partition on the slave

### `esp_hosted_slave_ota_write()`

```c
esp_err_t esp_hosted_slave_ota_write(const void *data, size_t size);
```

Sends firmware data chunks to the slave.

* Arguments:

  * `data`: Pointer to firmware data chunk
  * `size`: Size of the data chunk (typically 1400–1500 bytes)
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Transmits firmware data over ESP-Hosted transport (SDIO/SPI/UART)
  * The slave writes data to its OTA partition
  * Can be called multiple times for large firmware images

### `esp_hosted_slave_ota_end()`

```c
esp_err_t esp_hosted_slave_ota_end(void);
```

Finalizes the OTA process.

* Arguments: None
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Validates the complete firmware image on the slave
  * Calculates and verifies checksums
  * Marks the new firmware as valid but not yet active

### `esp_hosted_slave_ota_activate()`

```c
esp_err_t esp_hosted_slave_ota_activate(void);
```

Activates the newly flashed firmware.

* Arguments: None
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Switches the slave’s boot partition to the new firmware
  * Triggers slave reboot with the new firmware
  * Note: After this call, the slave restarts with the new firmware

## How to use the new APIs

A dedicated example demonstrates the usage of the new OTA APIs:
[Slave OTA using ESP-Hosted transport](../examples/host_performs_slave_ota/README.md)

> [!TIP]
> The example uses the new ESP-Hosted-MCU Slave OTA APIs.
> You can reuse or customize it for your own OTA workflow.

Example methods supported:

| Method           | Description                                                                                                                                 |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Partition method | Slave firmware binary stored in Host’s partition table (`slave_fw` partition). Requires an extra host partition, but no Wi-Fi connectivity. |
| LittleFS method  | Host partition formatted as LittleFS and stores the slave firmware. Requires an extra host partition, but no Wi-Fi connectivity.            |
| HTTPS method     | Slave firmware binary hosted on an HTTPS server. No extra host partition needed, but requires Wi-Fi connectivity.                           |

# $${\color{yellow} \text{2.5.2 - Bluetooth Controller on Co-Processor Disabled by Default}}$$

## Migration needed from versions

| Slave version | Host version |
| ------------- | ------------ |
| > 2.5.1       | > 2.5.1      |

Before v2.5.2, the Bluetooth controller on the co-processor was initialized and enabled by default.
From v2.5.2 onwards, it starts in a disabled state.

## Reason for change

This allows users to modify the Bluetooth MAC address before the controller is initialized, as it can only be changed prior to enabling the controller.

## New APIs

### `esp_hosted_bt_controller_init()`

```c
esp_err_t esp_hosted_bt_controller_init(void);
```

Initializes the Bluetooth controller on the co-processor.

* Arguments: None
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Allocates and initializes controller resources
  * Prepares the controller for activation

### `esp_hosted_bt_controller_deinit()`

```c
esp_err_t esp_hosted_bt_controller_deinit(bool mem_release);
```

Deinitializes the Bluetooth controller on the co-processor.

* Arguments:

  * `mem_release`: If true, releases controller memory (cannot be reused)
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Stops the Bluetooth controller
  * Optionally releases memory used by the controller
  * Once released, the controller cannot be reinitialized without reboot

### `esp_hosted_bt_controller_enable()`

```c
esp_err_t esp_hosted_bt_controller_enable(void);
```

Enables the Bluetooth controller on the co-processor.

* Arguments: None
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Starts the Bluetooth controller task
  * Enables radio and HCI interfaces for Bluetooth operation

### `esp_hosted_bt_controller_disable()`

```c
esp_err_t esp_hosted_bt_controller_disable(void);
```

Disables the Bluetooth controller on the co-processor.

* Arguments: None
* Returns: `ESP_OK` on success, or an error code on failure
* What it does:

  * Gracefully stops the controller
  * Disables the Bluetooth radio
  * Must be called before deinitializing the controller

## Changes required on host

Before starting the Bluetooth stack on the host:

1. Call `esp_hosted_connect_to_slave()` to establish a connection with the slave.
2. (Optional) Set the Bluetooth MAC address using `esp_hosted_iface_mac_addr_set()`.
3. Initialize the Bluetooth controller using `esp_hosted_bt_controller_init()`.
4. Enable the Bluetooth controller using `esp_hosted_bt_controller_enable()`.

See [Initializing the Bluetooth Controller](bluetooth_design.md#31-initializing-the-bluetooth-controller) for more details.

## How to use the new APIs

You can now start the host Bluetooth stack and use Bluetooth as usual.
All ESP-Hosted Bluetooth host examples (NimBLE and BlueDroid) have been updated accordingly.

For an example showing how to change the BT MAC address before starting the controller, refer to:
[BT Controller Example](../examples/host_bt_controller_mac_addr/)
