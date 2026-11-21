# Troubleshooting

[Home](../README.md) · [Getting Started: Linux](getting-started-linux.md) · [Getting Started: MCU](getting-started-mcu.md) · **Troubleshooting**

When something breaks, debug **bottom-up**: wiring → transport → handshake → feature. The playbook below follows that order — resolve each numbered item before moving to the next.

**Table of Contents**

- [1. ESP host already has native Wi-Fi](#1-esp-host-already-has-native-wi-fi)
- [2. Raw throughput testing](#2-raw-throughput-testing)
- [3. Keep host and slave code in sync](#3-keep-host-and-slave-code-in-sync)
- [4. Make GPIOs match on both ends](#4-make-gpios-match-on-both-ends)
- [5. Master not connecting to slave](#5-master-not-connecting-to-slave)
- [6. `Drop Packet` errors](#6-drop-packet-errors)
- [7. References](#7-references)

---

## 1. ESP host already has native Wi-Fi

Sometimes you have two ESPs but **both** have native Wi-Fi. To run ESP-Hosted on an ESP host chipset that already supports Wi-Fi natively, disable native Wi-Fi in the ESP-IDF you build against.

**1.1 — Host and slave are different ESP chipsets.** Disable Wi-Fi for the host chipset alone. Edit `components/soc/<soc>/include/soc/Kconfig.soc_caps.in` in ESP-IDF and set every `WIFI`-related config to `n`:

```
config SOC_WIFI_SUPPORTED
    bool
    # default y # original configuration
    default n
```

Do this for **all** `SOC_WIFI_xxx` configs in the file. Chipsets without native Wi-Fi already have these set to `n`.

**1.2 — Host and slave are the same ESP chipset** (e.g. two ESP32-C3). This is a two-step build: build for the host with the changes from 1.1 applied, flash and monitor it, then **revert all the changes** and flash the slave chipset.

---

## 2. Raw throughput testing

When chasing performance — and when checking correctness — you need to know the transport's bottleneck. **Raw throughput (RawTP)** is transport-level testing: dummy data streams from one end to the other with **no** Wi-Fi, Bluetooth, or other code involved. It runs three ways:

- **Host → slave** (half duplex): dummy data host-to-slave, continuously.
- **Slave → host** (half duplex): dummy data slave-to-host, continuously.
- **Bidirectional** (full duplex): both directions simultaneously.

This verifies hardware signal integrity, surfaces porting issues, and reveals the achievable throughput of the solution — and can be used for transport fine-tuning.

> [!IMPORTANT]
> Use the raw throughput test to verify that the Hosted hardware and software work as expected **before** involving other software layers like networking.

**Enable RawTP on the slave:** `Menuconfig` → **Example Configuration** → **Hosted Debugging** → **RawTP**.

**Enable RawTP and set direction on the host:** `Menuconfig` → **Component config** → **ESP-Hosted config** → **Debug Settings** → **RawTP**. Set the direction: **Host to Slave**, **Slave to Host**, or **Bidirectional**.

---

## 3. Keep host and slave code in sync

The README instructions fetch the latest ESP-Hosted from the Component Registry, which is usually fine. To pin a fixed version instead, fetch by revision.

Fetch version 0.0.9 of the ESP-Hosted **master**:

```
idf.py add-dependency "espressif/esp_hosted^0.0.9"
```

Fetch version 0.0.9 of the ESP-Hosted **slave**:

```
idf.py create-project-from-example "espressif/esp_hosted^0.0.9:slave"
```

This keeps master and slave fixed and in sync. Prefer the latest versions for bug fixes.

> [!NOTE]
> When switching Hosted versions, use the **same** version for master and slave. Implementation changes can make mismatched master/slave versions incompatible.

---

## 4. Make GPIOs match on both ends

- Check that the GPIOs on host and slave are correct and physically connected together as expected.
- Verify that the GPIO values set in `menuconfig` match the hardware GPIOs you are actually using.
- Avoid incompatible GPIOs:
  - on the ESP32, some GPIOs are input-only and cannot be used for output;
  - on the ESP32 and ESP32-C6, the GPIOs used for SDIO are **fixed** and cannot be changed.

---

## 5. Master not connecting to slave

Over **SPI**, if the master console shows:

```
E (10645) transport: Not able to connect with ESP-Hosted slave device
```

or over **SDIO**:

```
E (1735) sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107
```

then something is wrong with the SPI/SDIO connection and the host cannot talk to the slave.

- Check the physical GPIO signals and verify they are connected.
- Ensure host and slave selected the **same** transport (both SPI, or both SDIO):
  - slave and host should use the exact same codebase (git commit);
  - the transport configured on the slave must match the host;
  - incompatible firmware configurations also cause this.
- Verify the physical GPIO signals match those assigned in `Menuconfig` on **both** host and slave.
- For SDIO on a classic **ESP32** host, watch for a conflict between the GPIO used to bootstrap the ESP32 and the one used by SDIO — see [References](#7-references).
- For SDIO, confirm pull-ups and signalling requirements (short, shielded connections) are met — see the [SDIO](getting-started-mcu.md#1-sdio) page.
- If your transport tolerates jumper cables, cross-check the maximum allowed jumper length in the [Getting Started: MCU](getting-started-mcu.md).

### If the error persists across reboots

A host-side reset is **not** a power cycle. The transport already resets the slave
(`CONFIG_ESP_HOSTED_SLAVE_RESET_ON_EVERY_HOST_BOOTUP`), but if the co-processor has ended up in
a wedged state, pulsing its reset/EN line may not bring it back: the host keeps reporting
`send_op_cond ... 0x107` and `card init failed` on every boot, and re-flashing the host does not help either.

If your board lets the host control the co-processor power/enable (some boards do it through an
IO expander), toggle it once and retry the transport init:

```c
enable_coprocessor_power(false);          /* cut the co-processor supply      */
vTaskDelay(pdMS_TO_TICKS(500));
enable_coprocessor_power(true);           /* back on                          */
vTaskDelay(pdMS_TO_TICKS(3000));          /* let the slave bring SDIO up      */
/* then re-init, e.g. esp_wifi_init() -> esp_wifi_remote_init() -> esp_hosted_reconfigure() */
```

Do this only when the link is already down, and restore the pin level immediately afterwards:
in some designs that pin also participates in power management. We have seen a completely
unresponsive co-processor recover on the first try this way (logs and discussion in issue #240).

### Note on restarts

`CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE` defaults to `y`, so an unrecoverable transport
error restarts the host, which then fails again - an endless reboot loop that hides the real
state. Consider disabling it and handling `ESP_HOSTED_EVENT_TRANSPORT_FAILURE` in the application
(see `examples/host_hosted_events`), so the application can run its own recovery, for example the
power cycle above.

---

## 6. `Drop Packet` errors

Over **SPI**, an error like:

```
I (478522) spi: rcvd_crc[30224] != exp_crc[36043], drop pkt
```

means the SPI interface is hitting signal-integrity errors.

- **Lower the SPI `CLK` frequency** (via `Menuconfig`) first. If the problem disappears, the physical SPI signals are the issue.
- Scope the SPI lines with an oscilloscope for noise, ringing, and other artifacts.

---

## 7. References

- [Conflicts Between Bootstrap and SDIO on DAT2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/sd_pullup_requirements.html#conflicts-between-bootstrap-and-sdio-on-dat2)
- [Getting Started: MCU](getting-started-mcu.md) — wiring rules and jumper limits.
- [SDIO](getting-started-mcu.md#1-sdio) · [SPI Full-Duplex](getting-started-mcu.md#2-spi-full-duplex) — per-bus pin maps and requirements.
- [Testing](testing.md) — RawTP fits under the smoke-test layer.
