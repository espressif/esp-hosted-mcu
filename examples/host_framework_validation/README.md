| Supported Hosts | ESP32 | ESP32-P Series | ESP32-H Series | ESP32-C Series | ESP32-S Series | Any other MCU hosts |
| --------------- | ----- | -------------- | -------------- | -------------- | -------------- | ------------------- |

| Supported Co-Processors | ESP32 | ESP32-C Series | ESP32-S Series |
| ----------------------- | ----- | -------------- | -------------- |

# ESP-Hosted Host Framework Validation Example

This example is the baseline validation target for the refactored host-side generic framework.

Its purpose is not to demonstrate Wi-Fi application logic. Its purpose is to prove that the host framework itself can be brought up, connected to the slave, observed through hosted events, and torn down cleanly under explicit application control.

The example performs a small and repeatable lifecycle probe:

- initialize NVS and the default event loop
- register an `ESP_HOSTED_EVENT` handler once at application startup
- call `esp_hosted_init()` explicitly
- call `esp_hosted_connect_to_slave()` explicitly
- wait for `ESP_HOSTED_EVENT_TRANSPORT_UP` and `ESP_HOSTED_EVENT_CP_INIT`
- optionally query the co-processor firmware version
- call `esp_hosted_deinit()` explicitly
- repeat the same lifecycle for a configurable number of cycles

This keeps validation focused on the host framework lifecycle itself, without mixing in station connection flow, heartbeat policy, recovery state machines, or application traffic.

Inside this repository, the example uses the local repository copy of `esp_hosted` through `components/esp_hosted`. That means a normal build of this example validates the current worktree by default, instead of the registry component.

## What This Example Is For

Use this example as the first acceptance check for the host generic framework on a target host platform.

If this example passes, you have evidence that the following baseline path works on that platform:

- the host-side framework can be initialized explicitly by the application
- the selected port layer and transport configuration are usable on that board
- the host can bring up the transport and communicate with the slave
- hosted lifecycle events reach the application correctly
- the framework can be deinitialized and initialized again in a later cycle

In other words, this example answers the question: "Can this platform run the host generic framework lifecycle reliably under explicit application control?"

## Acceptance Target

The example should be considered passed only when all configured validation cycles complete and the final summary matches the expected lifecycle counts.

For each cycle, the acceptance target is:

- `esp_hosted_init()` returns success
- `esp_hosted_connect_to_slave()` returns success
- both `ESP_HOSTED_EVENT_TRANSPORT_UP` and `ESP_HOSTED_EVENT_CP_INIT` are observed before the timeout
- `esp_hosted_deinit()` returns success

At the end of the run, the summary should show:

- `cp_init == validation cycle count`
- `transport_up == validation cycle count`
- `transport_down == validation cycle count`
- `transport_failure == 0`
- final log prints `host framework validation passed`

If the slave firmware is very old and does not support `GetCoprocessorFwVersion`, the firmware-version RPC may time out. That warning does not fail this example, because firmware-version query is not the primary acceptance signal.

## Host Framework Capabilities Validated

This example validates these host generic framework capabilities:

- explicit framework lifecycle control from the application through `esp_hosted_init()`, `esp_hosted_connect_to_slave()`, and `esp_hosted_deinit()`
- correct creation and use of the host-side event path for `ESP_HOSTED_EVENT`
- transport bring-up from the host to the slave using the configured port and transport implementation
- delivery of core lifecycle events: `TRANSPORT_UP`, `CP_INIT`, and `TRANSPORT_DOWN`
- repeatable re-entry of the framework across multiple init/connect/deinit cycles
- basic post-connect RPC viability through the optional firmware-version request

## What This Example Does Not Validate

This example is intentionally not a recovery or feature-completeness test. It does not validate:

- Wi-Fi station or AP data-plane operation
- Bluetooth feature operation
- host-driven slave reset and recovery policy
- automatic reconnect logic after transport loss
- heartbeat monitoring policy
- application-level network traffic such as ping, iperf, or socket workloads

Those behaviors should be covered by separate examples so that baseline framework validation stays small and easy to diagnose.

## Default Hardware Profile

The provided `sdkconfig.defaults` is tuned for the ESP32-P4 function EV board using the on-board ESP32-C6 as the slave over SDIO:

- SDIO host interface
- 4-bit bus
- 40 MHz SDIO clock
- slave reset GPIO 54
- transport auto-restart disabled so the example owns the lifecycle explicitly

Adjust these values in menuconfig if your board wiring differs.

## Build

This example already targets the local repository copy of `esp_hosted` by default through `components/esp_hosted`, so no extra manual setup is required.

```bash
cd examples/host_framework_validation
idf.py set-target esp32p4
idf.py build
```

## Configure

Open menuconfig:

```bash
idf.py menuconfig
```

In `Example Configuration` you can change:

- validation cycle count
- connect wait timeout
- delay between cycles
- whether to try the optional firmware-version RPC

## Expected Behaviour

On a healthy link you should see, for each cycle:

- `esp_hosted_init()` succeeds
- `esp_hosted_connect_to_slave()` succeeds
- `got TRANSPORT_UP event`
- `got CP_INIT event from co-processor`
- optional firmware version output, or a warning if the slave is too old
- `esp_hosted_deinit()` succeeds

At the end of a successful run, the summary counts should match the configured cycle count and `transport_failure` should remain `0`.

If the slave firmware is too old to support `GetCoprocessorFwVersion`, the example will log a warning and continue. That RPC is not used as the primary pass/fail signal.