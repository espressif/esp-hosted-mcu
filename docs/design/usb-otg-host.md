# [Experimental] USB OTG Host Transport (CDC-ACM + RNDIS [Not yet enabled.])

> [!NOTE]
> The USB-OTG interface is currently experimental. Code and configuration is in active development and subject to change.

[Home](../../README.md) · [Getting Started: MCU](../getting-started-mcu.md) · [Architecture](../architecture.md) · [UART Design](uart.md)

The MCU host can use USB OTG Host as the ESP-Hosted bus. The coprocessor is a
USB device; the host enumerates and opens its CDC data interface. By default,
all ESP-Hosted frames, including RPC and Wi-Fi, travel over CDC-ACM.

[USB RNDIS is not yet enabled.] The host and coprocessor Kconfig symbols remain, and `depends on n` keeps them off. When both
`ESP_HOSTED_HOST_USB_RNDIS` and
`EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS` are enabled, CDC-ACM carries RPC,
serial, HCI, power-save, and flow-control frames while one RNDIS function
carries raw STA/AP Ethernet frames. RNDIS currently requires ESP32-P4 and the
ESP-IDF CDC-ACM host stack; CherryUSB remains CDC-only.

---

## Layers

```
examples/system/api_exerciser/mcu_host          application (eh_host_* only)
        |
eh_host_bus_usb.c                               MCU bus contract
        frame RX task, stream buffer, blocking TX
        |
        +-- eh_host_bus_usb_cdc_acm.c           usb_host_cdc_acm (control)
        |     eh_host_port_usb.c                usb_host_install, event task
        |     espressif/usb
        +-- eh_host_bus_usb_cdc_rndis.c         iot_usbh_cdc (control, with RNDIS)
        +-- eh_host_bus_usb_rndis.c             iot_usbh_rndis (STA/AP)
        |
        +-- eh_host_bus_usb_cherryusb.c         CherryUSB DWC2 host + CDC-ACM serial
              usbh_initialize, /dev/ttyACM0
```

The stack is `ESP_HOSTED_HOST_USB_STACK_CDC_ACM` (default) or `ESP_HOSTED_HOST_USB_STACK_CHERRYUSB`. The generic bus file is always built for this bus. CDC-ACM compiles `eh_host_bus_usb_cdc_acm.c`, or `eh_host_bus_usb_cdc_rndis.c` and `eh_host_bus_usb_rndis.c` when RNDIS is selected. CherryUSB compiles only `eh_host_bus_usb_cherryusb.c`.

`eh_host_port_usb_init()` / `eh_host_port_usb_deinit()` are reference-counted and exist only for the ESP-IDF stack. A later class driver on that stack can share the same host controller without installing it again. CherryUSB owns the controller itself and does not call `usb_host_install()`.

---

## Data path

**Connect.** `eh_host_bus_connect_to_slave()` resets the coprocessor when a reset GPIO is configured, then opens the CDC-ACM device. With RNDIS disabled, the ESP-IDF stack opens any device (`CDC_HOST_ANY_VID`, `CDC_HOST_ANY_PID`). With RNDIS enabled it matches `0x303A:0x4081` and opens the advertised CDC-ACM interface. See [Composite interfaces](#composite-interfaces). CherryUSB polls `usbh_serial_open("/dev/ttyACM0")` until the connection timeout. Both assert DTR (RTS stays clear) so the coprocessor starts the data path and sends INIT. CherryUSB does not set line coding. Transport state becomes `EH_HOST_MCU_TRANSPORT_RX_ACTIVE`.

CherryUSB's CDC-ACM attach requires the notification interrupt endpoint. TinyUSB CDC-ACM on the coprocessor provides that endpoint. If the device enumerates but the serial port never appears, that endpoint is the first thing to check.

**Receive.** Incoming bytes are copied into a FreeRTOS stream buffer. The ESP-IDF stack does that from the CDC data callback. CherryUSB completes bulk IN in the controller ISR, and a pump task copies the DMA buffer into the same stream, then re-arms the transfer. `eh_usb_rx_task` reads a fixed header, takes the little-endian payload length, reads that many bytes, calls `eh_frame_decode()`, and dispatches with `eh_host_mcu_transport_dispatch_frame()`. HCI frames (`ESP_HCI_IF`) go to `eh_host_mcu_hci_rx_deliver()`. Device disconnect tears the link down and posts a transport failure. On the coprocessor, ending the CDC session soft-resets the chip. USB suspend leaves it running. See [Session end](#session-end) and [USB suspend](#usb-suspend).

**Transmit.** With RNDIS disabled, `eh_host_bus_tx()` encodes every
`interface_buffer_handle_t` into an `eh_frame` and sends it on CDC. With
RNDIS enabled, nonempty STA/AP payloads bypass `eh_frame` and enter a bounded
RNDIS TX worker queue; the worker serializes bulk OUT writes. A full queue or
a disconnected RNDIS function returns a Wi-Fi TX failure to lwIP, preserving
backpressure rather than accumulating buffers. All other traffic remains
framed CDC. `eh_host_bus_is_tx_ready()` is true while the CDC device is open.

**RNDIS receive.** The RNDIS driver strips its encapsulation and calls the
normal hosted channel dispatcher with the raw Ethernet frame. STA/AP is
selected from the source MAC: it is AP only when it matches the CP SoftAP MAC,
the same convention used by the CP transmit path. The dispatcher copies the
frame before returning to the RNDIS driver.

Framing matches the other stream buses: no delimiter or resync byte. See [Architecture](../architecture.md) and [UART Design](uart.md).

---

## Composite interfaces [Not yet enabled.]

CDC-only firmware does not use the RNDIS configuration descriptor. esp_tinyusb's default descriptor puts CDC-ACM communications on interface 0 and the CDC data interface on interface 1. The host opens that first CDC interface with `cdc_acm_host_open()`.

With `EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS` and `ESP_HOSTED_HOST_USB_RNDIS` both enabled, the coprocessor advertises one composite configuration, product ID `0x4081`:

| Interface | Function |
| :--- | :--- |
| 0 | RNDIS communications |
| 1 | RNDIS data |
| 2 | CDC-ACM communications |
| 3 | CDC-ACM data |

Endpoint numbers stay with the function. CDC notification and data are endpoints 1 and 2. RNDIS notification and data are endpoints 3 and 4.

The host CDC path selects the communications interface whose class is `0x02`, subclass is ACM `0x02`, and protocol is `0x00`. It opens that interface and uses the same number as `wIndex` for `SET_CONTROL_LINE_STATE`. If that interface is missing, the log is `CDC-ACM interface not advertised`. A successful open logs `CDC-ACM interface` and the interface number.

`espressif/iot_usbh_rndis` `^0.5.2` is the managed component from the component registry. It opens the wireless RNDIS interface association and sends class requests with `wIndex` 0. That is the RNDIS communications interface in this layout.

---

## Session end

[`eh_cp_transport_usb_otg_device.c`](../../coprocessor/eh_cp_transport/src/eh_cp_transport_usb_otg_device.c) soft-resets the coprocessor with `esp_restart()` when the host ends the CDC session. That is the same reset the slave-reset GPIO uses. The next connect then enumerates a fresh boot and receives a new INIT. `esp_restart()` does not return, so the first matching signal is the one that runs. The log is `USB disconnect (DTR), resetting` or `USB disconnect (detached), resetting`.

Two signals count, and only after the session has started:

- **DTR drop.** `usb_on_dtr()` resets when DTR falls after the host had asserted it. The host close path clears DTR first (`cdc_acm_host_set_control_line_state(false)`, or CherryUSB clearing `TIOCM_DTR`) and then releases the device. A DTR fall before the first assert does nothing.
- **USB detach.** `usb_tinyusb_event_cb()` latches `usb_configured` on `TINYUSB_EVENT_ATTACHED`, which is the host's `SET_CONFIGURATION`. `TINYUSB_EVENT_DETACHED` after that latch takes the same reset. A detach before the first configuration does nothing, so a session-end interrupt at startup cannot reboot-loop.

Host power-save bus unload (`EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING`) sets `usb_exit_requested` before `tinyusb_driver_uninstall()`. Uninstall deinits TinyUSB and can deliver detach while the device is still configured. The reset helper returns while that flag is set. Deinit also unregisters the CDC line-state callback before uninstall, so the DTR path stays quiet during unload.

---

## USB suspend

USB suspend is a bus-idle event, separate from detach. The DWC2 device controller raises suspend when start-of-frame packets stop. TinyUSB reports that as `TINYUSB_EVENT_SUSPENDED` (`tud_suspend_cb`). The transport callback handles attach and detach only, so suspend and resume leave the coprocessor running. Wi-Fi, RPC, `usb_host_dtr`, and the configured latch stay as they were. A host that suspends the bus and later resumes it continues the same session. A later DTR rising edge opens the data path only if DTR is clear.

On ESP32-S31 the high-speed USB OTG peripheral does not sense VBUS. This transport installs TinyUSB with `phy.self_powered` clear and no `vbus_monitor_io`, so the GPIO VBUS monitor stays off. The DWC2 device driver posts an unplug on OTG session-end (`GOTGINT_SEDET`). Its disconnect-interrupt path (`GINTSTS_DISCINT`) is still a TODO in the TinyUSB port. Pulling the cable, or lifting the coprocessor off a powered host, stops SOFs and arrives as USB suspend. The usb_dongle example logs that same callback as `USB suspended`. The host is already gone, so it cannot clear DTR. Neither soft-reset path runs, and the chip stays up until a later DTR drop, a real detach, or the reset GPIO.

Cable removal becomes a detach when VBUS reaches a coprocessor GPIO and TinyUSB is installed self-powered with that pin as `vbus_monitor_io`. The monitor then posts `DCD_EVENT_UNPLUGGED`, which becomes `TINYUSB_EVENT_DETACHED` and takes the detach reset above. This transport does not wire that GPIO.

---

## RX backpressure

Host-to-coprocessor Wi-Fi traffic (STA/AP upload) is faster than the coprocessor can hand frames to Wi-Fi. Two mechanisms keep the coprocessor RX pool from dropping those frames. Both live on the V1 header USB uses (`EH_FRAME_CFG_*_USB_DEFAULT`). RPC and serial are not throttled.

**Credits (coprocessor).** [`eh_cp_transport_usb_otg_device.c`](../../coprocessor/eh_cp_transport/src/eh_cp_transport_usb_otg_device.c) sizes the RX mempool and the RX stream to `RX_Q_SIZE + USB_RX_MEMPOOL_MARGIN` (`USB_RX_MEMPOOL_MARGIN` is 2). That is the same slack-over-queue shape as SDIO: one block for `usb_rx_task` until the queue accepts the frame, and one for `recv_task` after dequeue through `wifi_tx_with_retry`. The priority queues themselves stay `RX_Q_SIZE` deep.

A counting semaphore starts full at that block count. `usb_rx_task` takes one credit before it pulls a frame out of the stream and allocates. `usb_rx_read_done` returns the credit after the pool block is freed. A bad-length or decode-drop path returns the credit without holding a block.

`usb_cdc_pull()` reads the TinyUSB CDC FIFO only while a credit is free and the stream can hold another full frame (`BUFFER_SIZE`). Otherwise it leaves the bytes in the FIFO. The device NAKs further OUT tokens, and the host's `cdc_acm_host_data_tx_blocking()` waits instead of the coprocessor dropping a partial frame. The CDC RX callback does not run again until that FIFO is read, so `usb_rx_read_done` and `usb_rx_task` re-arm the pull after a credit or stream space returns. A mutex (`usb_cdc_lock`) keeps the callback and `recv_task` from reading CDC at the same time.

**Throttle (coprocessor to host).** When `usb_rx_queue[PRIO_Q_OTHERS]` crosses the high/low percents from the host caps packet (`THROTTLE_HIGH` / `THROTTLE_LOW`; 0 disables it), `usb_flow_ctrl` queues a header-only V1 frame with `throttle_cmd` set to `EH_FRAME_FLOW_CTRL_ON` or `EH_FRAME_FLOW_CTRL_OFF`. Upload has almost no reverse data, so the signal is not piggybacked on a data frame. The task is separate from `usb_rx_task` so RX can keep running while CDC TX holds `usb_tx_lock`.

`decode_v1` keeps `throttle_cmd` on both zero-length returns. A flagged `len == 0` frame (power save) still goes through checksum validation first; an unflagged header-only frame, including throttle, returns immediately with `flags` cleared and `throttle_cmd` copied.

**Host.** `eh_usb_rx_task` maps `EH_FRAME_FLOW_CTRL_ON` / `OFF` to `eh_host_wifi_tx_set_throttle()` for both `EH_FRAME_OK` and `EH_FRAME_DUMMY`. While throttled, `eh_host_transport_tx()` returns `ESP_ERR_ESP_NETIF_NO_MEM` for `ESP_STA_IF` and `ESP_AP_IF`, so the IP stack backs off before the USB TX timeout. The NAK path above still covers packets already on the bus, including raw throughput, which does not consult the throttle flag.

The USB start threshold is `ESP_HOSTED_PRIV_WIFI_TX_USB_HIGH_THRESHOLD` (default 80), selected into `ESP_HOSTED_TO_WIFI_DATA_THROTTLE_HIGH_THRESHOLD` when the bus is USB OTG host. The stop threshold stays the shared `ESP_HOSTED_TO_WIFI_DATA_THROTTLE_LOW_THRESHOLD` (default 60).

### RNDIS upload backpressure [Not yet enabled.]

`USB_OTG_DRIVER: rndis rx drop, no credit` is printed by `usb_rndis_rx_cb()`
in [`eh_cp_transport_usb_otg_device.c`](../../coprocessor/eh_cp_transport/src/eh_cp_transport_usb_otg_device.c)
when a host-to-coprocessor RNDIS frame arrives and `usb_rx_credits` cannot be
taken. Every receive credit is already outstanding. This is host-to-coprocessor
receive saturation. A coprocessor-to-host TinyUSB IN retry is a different path
and does not print this line.

`tud_network_recv_cb()` calls `usb_rndis_rx_cb()` and then returns false, which
re-arms the bulk OUT transfer. CDC can leave unread bytes in its FIFO so the
device NAKs further OUT tokens. RNDIS has no such hold, so a frame that cannot
take a credit is discarded and the next OUT transfer is armed immediately.

`usb_rx_credits` is a counting semaphore sized to
`EH_TRANSPORT_CP_USB_OTG_DEVICE_RX_Q_SIZE + 2` (`USB_RX_MEMPOOL_MARGIN`). The
default queue is 5, so there are 7 credits. CDC RX and RNDIS RX share that
semaphore. A credit stays taken while the frame sits on a priority queue or is
held through Wi-Fi TX, including `wifi_tx_with_retry`.

The same callback prints a different line for each other failure:
`rndis rx drop, queue full` when a credit was free but `PRIO_Q_OTHERS` rejected
the frame, `rndis rx drop, no buffer` when a credit was taken but the mempool
allocation failed, and `rndis rx drop len` when the length is outside the
Ethernet header and MTU limits.

These options change how soon `rndis rx drop, no credit` appears:

- Coprocessor `EH_TRANSPORT_CP_USB_OTG_DEVICE_RX_Q_SIZE` (default 5) raises the
  shared credit count by the same amount. A larger queue uses more RAM and
  absorbs a longer burst before credits run out.
- Host `ESP_HOSTED_HOST_USB_RNDIS_TX_Q_SIZE` (default 20, range 4–100) is how
  many Ethernet frames lwIP may queue while the RNDIS bulk OUT worker is busy.
  Frames already accepted there can still arrive after the coprocessor credits
  are exhausted. Lower it so lwIP is rejected sooner. The default is larger
  than the default coprocessor credit count.
- Host `ESP_HOSTED_HOST_TO_ESP_WIFI_DATA_THROTTLE` (default y),
  `ESP_HOSTED_PRIV_WIFI_TX_USB_HIGH_THRESHOLD` (default 80; 0 disables), and
  `ESP_HOSTED_TO_WIFI_DATA_THROTTLE_LOW_THRESHOLD` (default 60) control the CDC
  V1 throttle signal. That signal stops new STA/AP submissions in lwIP. It does
  not reclaim frames already sitting in the host RNDIS TX queue. Setting the
  high threshold to 0 removes that brake.

When packet stats are enabled, compare host `RNDIS tx_ok` and `tx_reject` with
coprocessor `H2S(in/out/fail)`. `tx_ok` ahead of `H2S(in)` is frames the host
submitted that the coprocessor did not admit. `H2S(in)` ahead of `H2S(out)`,
or a rising `H2S(fail)`, means credits are held in Wi-Fi TX.

---

## Configuration

Select the bus with `CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_OTG_HOST=y`. That sets `CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_OTG_HOST_READY` when the MCU transport is compiled in.

Kconfig under **USB OTG Host Configuration (Experimental)**:

| Symbol | Default | Meaning |
| :--- | :--- | :--- |
| `ESP_HOSTED_HOST_USB_STACK_CDC_ACM` | y | ESP-IDF USB Host Library and `usb_host_cdc_acm` |
| `ESP_HOSTED_HOST_USB_STACK_CHERRYUSB` | n | CherryUSB DWC2 host and its CDC-ACM serial driver. Selects `CHERRYUSB`, `CHERRYUSB_HOST`, and `CHERRYUSB_HOST_CDC_ACM` |
| `ESP_HOSTED_HOST_RESET_GPIO` | 54 on ESP32-P4, else board-specific or `-1` | Coprocessor reset; `-1` skips reset |
| `ESP_HOSTED_HOST_USB_CONNECTION_TIMEOUT_MS` | 5000 | Wait for CDC enumeration |
| `ESP_HOSTED_HOST_USB_TX_Q_SIZE` / `RX_Q_SIZE` | 20 | Queue depths |
| `ESP_HOSTED_HOST_USB_OUT_BUF_SIZE` | 2048 | Bulk OUT size |
| `ESP_HOSTED_HOST_USB_IN_BUF_SIZE` | 0 | Bulk IN size; 0 uses endpoint MPS |
| `ESP_HOSTED_HOST_USB_RX_TASK_STACK` | 4096 | RX task stack |
| `ESP_HOSTED_HOST_USB_RX_TASK_PRIO` | 22 | RX task priority |
| `ESP_HOSTED_HOST_USB_RNDIS` | n | [Not yet enabled.] P4 ESP-IDF RNDIS data plane. `depends on n`, so it cannot be selected |
| `ESP_HOSTED_HOST_USB_RNDIS_TX_Q_SIZE` | 20 | [Not yet enabled.] Bounded RNDIS upload queue |

Runtime defaults are `eh_host_get_default_usb_otg_config()` / `INIT_DEFAULT_HOST_USB_OTG_CONFIG()`. Transport id is `EH_HOST_TRANSPORT_USB_OTG` (`5`). Frame config is `EH_FRAME_CFG_HOST_MCU_USB_DEFAULT` (`ESP_TRANSPORT_USB_MAX_BUF_SIZE`, 1600).

On ESP32-P4, keep the console on USB Serial JTAG so the USB OTG peripheral is free for this transport. `sdkconfig.defaults.esp32p4.usb` sets that.

---

## Build check

From `examples/system/api_exerciser/mcu_host`:

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4.usb" set-target esp32p4
idf.py build
```

In `examples/wifi/iperf/cp`, the ESP32-S31 defaults do not select USB. For USB, append `sdkconfig.defaults.esp32s31.usb_cdc` to the CP defaults.

USB RNDIS is [Not yet enabled.] For RNDIS, append `sdkconfig.defaults.esp32p4.usb_rndis` to the host defaults
and append `sdkconfig.defaults.esp32s31.usb_rndis` to the CP defaults. These
options must be enabled as a pair: CP RNDIS moves STA/AP off CDC, so a
CDC-only host cannot carry Wi-Fi data.

The default `sdkconfig` has `CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_OTG_HOST=y`, `CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_OTG_HOST_READY=y`, and `CONFIG_ESP_HOSTED_HOST_USB_STACK_CDC_ACM=y`. That link uses `espressif__usb` and `espressif__usb_host_cdc_acm`.

CherryUSB is the other stack. Add `CONFIG_ESP_HOSTED_HOST_USB_STACK_CHERRYUSB=y` (and leave the CDC-ACM symbol unset) before configuring. That build links `cherry-embedded__cherryusb`, does not compile `eh_host_port_usb.c`, and does not require `usb_host_cdc_acm`.

### Managed components

`idf_component.yml` downloads a USB component only when the matching option is selected. The target rule still comes first.

| Component | Targets | Fetched when |
| :--- | :--- | :--- |
| `espressif/esp_tinyusb` | ESP32-S2, S3, S31 | `EH_TRANSPORT_CP_USB_OTG_DEVICE` |
| `espressif/usb_host_cdc_acm` | ESP32-P4 | `ESP_HOSTED_HOST_USB_STACK_CDC_ACM` and not `ESP_HOSTED_HOST_USB_RNDIS` |
| `espressif/iot_usbh_rndis` `^0.5.2` | ESP32-P4 | [Not yet enabled.] `ESP_HOSTED_HOST_USB_RNDIS` |
| `cherry-embedded/cherryusb` `>=1.6.0` | ESP32-P4 | `ESP_HOSTED_HOST_USB_STACK_CHERRYUSB` |

Component Manager reads `sdkconfig.json`. A USB menu symbol is omitted when that menu is inactive, and a hidden bool that is `n` is omitted too, so a `$CONFIG{}` rule on those symbols cannot tell "off" from "not yet configured". The root `Kconfig` therefore publishes a promptless int for each component (`ESP_HOSTED_CP_TINYUSB_MANIFEST`, `ESP_HOSTED_HOST_USB_CDC_ACM_MANIFEST`, `ESP_HOSTED_HOST_USB_RNDIS_MANIFEST`, `ESP_HOSTED_HOST_USB_CHERRYUSB_MANIFEST`). Each int is written on every build, including when the value is 0, and the manifest rule is `$CONFIG{..._MANIFEST} == 1`.

Selecting `EH_TRANSPORT_CP_USB_OTG_DEVICE` makes the promptless Kconfig default of `ESP_HOSTED_CP_TINYUSB_MANIFEST` equal 1, and kconfgen writes that into `sdkconfig.json`. An assignment of that symbol in `sdkconfig.defaults` has no effect.

A `dependencies.lock` created before these rules still lists the components, and ESP-IDF builds every managed component that remains on disk. Delete that lock and configure again. Component Manager then skips the unused dependencies and deletes their directories.
