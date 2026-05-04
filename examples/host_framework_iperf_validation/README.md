| Supported Hosts | ESP32 | ESP32-P Series | ESP32-H Series | ESP32-C Series | ESP32-S Series | Any other MCU hosts |
| --------------- | ----- | -------------- | -------------- | -------------- | -------------- | ------------------- |

| Supported Co-Processors | ESP32 | ESP32-C Series | ESP32-S Series |
| ----------------------- | ----- | -------------- | -------------- |

# ESP-Hosted Host Framework iPerf SoftAP Validation Example

This example is the upper-bound validation target for the current host generic framework refactor.

It goes beyond baseline transport lifecycle validation and exercises the main path the refactor was meant to enable:

- host starts the framework explicitly
- host uses `esp_hosted_connect_to_slave()` to bring up the slave through the configured reset-aware transport path
- application observes `ESP_HOSTED_EVENT_TRANSPORT_UP` and `ESP_HOSTED_EVENT_CP_INIT`
- host configures remote Wi-Fi into SoftAP mode
- host starts the iPerf console and remote control server
- an external test script or `iperf2` client drives TCP or UDP throughput tests over the hosted Wi-Fi link

This example is intentionally focused on one integrated validation path: host lifecycle, slave bring-up, control plane, SoftAP bring-up, and data-plane throughput.

## What This Example Is For

This example is not just another Wi-Fi demo. It is the current **Tier 3 hardware acceptance example** for the host generic framework refactor on an ESP host platform.

Its role is to answer one practical question:

- can the refactored host framework bring up the slave through the public framework path and sustain real data-plane traffic, not just lifecycle logs?

In other words, this example exists to prove that the refactor works as an integrated system, not merely that individual APIs compile or that transport comes up once.

## When The Host Generic Framework Can Be Considered Verified By This Example

For the current refactor phase, the host generic framework should be considered **verified on the current ESP host validation platform** only when both of the following are true:

1. **Control-plane acceptance is met**
   - `esp_hosted_init()` succeeds
   - `esp_hosted_connect_to_slave()` succeeds
   - `ESP_HOSTED_EVENT_TRANSPORT_UP` is observed
   - `ESP_HOSTED_EVENT_CP_INIT` is observed
   - SoftAP starts successfully
   - the remote iperf control service reports ready

2. **Data-plane acceptance is met**
   - the external test host can associate to the SoftAP
   - the validation flow can complete TCP RX, TCP TX, UDP RX, and UDP TX throughput runs
   - no host-side crash, fatal assert, or framework teardown occurs during the run

If both conditions are met, then this example demonstrates that the current ESP host port has passed the main end-to-end validation path for the host generic framework.

## What A Pass Means And What It Does Not Mean

A successful run of this example means:

- the current host generic framework design is working on the validated ESP host platform
- the public framework bring-up path is sufficient to support real hosted Wi-Fi traffic
- the refactor has crossed the boundary from architectural intent into working integrated behavior

A successful run of this example does **not** mean:

- every future MCU host port is already validated
- every ESP host variant is automatically validated
- every transport, recovery policy, or feature set is covered

So the right conclusion is:

- **this example can validate the main framework path on the current platform**
- **it cannot replace platform-by-platform follow-up validation**

## What This Example Validates

This example validates the following host framework capabilities together:

- explicit host framework lifecycle through `esp_hosted_init()` and `esp_hosted_connect_to_slave()`
- host-driven slave boot sequence through the public hosted connect path and configured reset GPIO
- delivery of hosted lifecycle events to the application
- remote Wi-Fi control from the host application
- SoftAP bring-up on the slave through standard `esp_wifi_*` APIs running on the host side
- local data-plane readiness for throughput measurement with `iperf2`
- external automation readiness through a small TCP remote-control service for iperf start and stop commands

For the current refactor scope, this is a much better proxy for the practical capability ceiling than a lifecycle-only probe.

## What This Example Does Not Validate

This example does not validate:

- repeated recovery loops after runtime transport failures
- heartbeat timeout policy
- automatic reconnect policy
- Bluetooth features
- host power save or network split specific features

Those should remain separate examples so this example stays centered on the main throughput-oriented host framework path.

## Default Hardware Profile

The provided `sdkconfig.defaults` is tuned for ESP32-P4 using the on-board ESP32-C6 as the slave over SDIO:

- SDIO host interface
- 4-bit bus
- 40 MHz SDIO clock
- slave reset GPIO 54
- transport restart on failure disabled so application-visible failures are not masked

## Acceptance Target

The example should be considered ready for throughput testing only after the monitor log shows all of the following:

- `esp_hosted_init()` succeeds
- `esp_hosted_connect_to_slave()` succeeds
- `ESP_HOSTED_EVENT_TRANSPORT_UP` is observed
- `ESP_HOSTED_EVENT_CP_INIT` is observed
- SoftAP start event is observed
- the example logs the SoftAP SSID and AP IP address
- the iperf remote control task reports that it is ready

Once those conditions are met, throughput is measured externally with `iperf2`.

## Recommended Pass Criteria For This Example

For day-to-day bring-up and refactor validation, treat the example as **passed** when the following checklist is satisfied in one run:

- build succeeds for the target host board
- monitor log shows framework init success, transport up, and CP init
- monitor log shows SoftAP start and remote control readiness
- the external machine successfully connects to the SoftAP
- the automation flow completes the intended iperf cases without framework crash or forced manual recovery
- the result summary is `PASS` or acceptable `WARN` for the intended cases

For the current ESP32-P4 + ESP32-C6 validation setup, a run completing all four directions (`tcp-rx`, `tcp-tx`, `udp-rx`, `udp-tx`) is the strongest evidence that the host generic framework main path is working on this platform.

## Build

This example uses the local repository copy of `esp_hosted` through `components/esp_hosted`, so a normal build validates the current worktree.

```bash
cd examples/host_framework_iperf_validation
idf.py set-target esp32p4
idf.py build
```

## Configure

Open menuconfig:

```bash
idf.py menuconfig
```

In `Example Configuration` you can change:

- hosted connect timeout
- SoftAP start timeout
- SoftAP SSID, password, channel, and max connection count
- whether to enable the iperf remote-control TCP service

## Test Flow

1. Flash the example to the host board and open the monitor.
2. Wait until the log reports:
   - hosted transport up
   - CP init event
   - SoftAP start
   - iperf remote control ready
3. Connect a PC or phone to the configured SoftAP.
4. Start an iperf server on the device either from the local console or through the remote-control port.
5. Run `iperf2` from the external test machine.

### Example Remote-Control Commands

The example starts a TCP control port on `22336`.

Basic checks:

```bash
printf 'PING\n' | nc 192.168.4.1 22336
printf 'STATUS\n' | nc 192.168.4.1 22336
```

Start a TCP server on the device:

```bash
printf 'START tcp-server 5001 3 60\n' | nc 192.168.4.1 22336
```

Then run `iperf2` on the external test machine:

```bash
iperf -c 192.168.4.1 -i 3 -t 60
```

For UDP, start the device side with:

```bash
printf 'START udp-server 5001 3 60\n' | nc 192.168.4.1 22336
```

Then run:

```bash
iperf -u -c 192.168.4.1 -i 3 -t 60 -b 20M
```

The remote-control protocol is intentionally tiny, so it is easy to drive from shell scripts.

## macOS Automation

For macOS, the cleanest path is not to port the Windows batch files directly. A small Python wrapper is a better fit because it is also usable on Linux and in future CI jobs.

This example includes [tools/run_iperf2.py](tools/run_iperf2.py), which does the following:

- talks to the DUT remote-control socket on port `22336`
- starts iperf server or client on the DUT
- runs local `iperf2` on the Mac for the opposite endpoint
- supports `tcp-rx`, `tcp-tx`, `udp-rx`, and `udp-tx`
- supports `all`, `quick-check`, and `continue-on-error` style batch runs
- prints a compact `PASS/WARN/FAIL` summary with Mbps, MB/s, and measured duration
- can optionally write JSON results

For a Finder-friendly entry point on macOS, the example also includes [run_iperf2_mac.command](run_iperf2_mac.command). Double-clicking that file opens Terminal and shows a menu closer to the Windows helper scripts. Besides choosing a test path, you can also edit DUT IP, duration, interval, UDP bandwidth, quick-check duration, inter-case cooldown, optional iperf path, and toggle `continue-on-error`.

### Prerequisites

Install `iperf2` locally. On macOS with Homebrew:

```bash
brew install iperf
```

Connect the Mac to the DUT SoftAP first, then verify the DUT control port is reachable.

### Typical macOS Flow

Run all four traffic directions:

```bash
cd examples/host_framework_iperf_validation
python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --cases all --time 30
```

Or, if you prefer not to type commands, double-click [run_iperf2_mac.command](run_iperf2_mac.command) in Finder and choose one of these menu paths:

- `All cases`
- `TCP RX`
- `TCP TX`
- `UDP RX`
- `UDP TX`
- `TCP RX + TCP TX`
- `UDP RX + UDP TX`
- `Quick check`
- `Edit settings`
- `Toggle continue-on-error`

Run only TCP RX and TCP TX:

```bash
python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --cases tcp-rx,tcp-tx
```

Run a short full sweep similar to the Windows `QuickCheck` flow:

```bash
python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --quick-check
```

Run all cases and continue even if one case fails:

```bash
python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --cases all --continue-on-error --inter-case-cooldown 5
```

Run UDP cases with a higher target bandwidth:

```bash
python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --cases udp-rx,udp-tx --udp-bandwidth 40
```

Write machine-readable results:

```bash
python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --cases all --result-json results.json
```

If automatic host IP detection fails, pass it explicitly:

```bash
python3 tools/run_iperf2.py --dut-ip 192.168.4.1 --host-ip 192.168.4.2 --cases all
```

### Direction Definitions

- `tcp-rx`: DUT runs iperf server, Mac runs iperf client, traffic flows from Mac to DUT
- `tcp-tx`: DUT runs iperf client, Mac runs iperf server, traffic flows from DUT to Mac
- `udp-rx`: DUT runs iperf server, Mac runs iperf client, traffic flows from Mac to DUT
- `udp-tx`: DUT runs iperf client, Mac runs iperf server, traffic flows from DUT to Mac

This is the recommended macOS test path because it stays close to the official iperf example model while directly exercising the hosted framework bring-up path added in this repository.
