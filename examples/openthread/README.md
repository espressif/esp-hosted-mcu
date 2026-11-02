# ESP-Hosted OpenThread examples

> [!NOTE]
> **These examples are for MCU / ESP-IDF hosts.** The Linux host does not support OpenThread.

## Hierarchy

```text
examples/openthread/<family>/<scenario>/<role>/
```

- `<family>`: where the 802.15.4 RCP runs.
  - `rcp_on_hosted_cp`: the ESP-Hosted co-processor is also the RCP. Two chips.
  - `rcp_separate_chip`: a separate chip is the RCP, on a dedicated spinel UART. The co-processor carries only Wi-Fi and BLE. Three chips.
- `<role>`: `cp` (ESP-Hosted co-processor), `esp_host` (ESP-IDF MCU host), `cp_rcp` (separate RCP chip).

```text
examples/openthread/
├── rcp_on_hosted_cp/          # Host + C6 or C5 (Wi-Fi and 802.15.4)
│   ├── cli/{cp,esp_host}
│   └── border_router/{cp,esp_host}
└── rcp_separate_chip/         # Host + C5 (Wi-Fi/BLE) + H2 or H21 (802.15.4)
    └── border_router/{cp,cp_rcp,esp_host}
```

## Which setup?

| Setup | Family | Spinel link | Use it when |
| :--- | :--- | :--- | :--- |
| B | `rcp_on_hosted_cp` | Dedicated UART to the ESP-Hosted co-processor, which is also the RCP | You want two chips: Wi-Fi and 802.15.4 on one co-processor. |
| C | `rcp_separate_chip` | Dedicated UART to the RCP chip | You want 802.15.4 on its own radio, with no coexistence with Wi-Fi or BLE. The RCP runs the ESP-IDF `ot_rcp` firmware. |

## Zigbee

[`examples/zigbee/thermostat`](../zigbee/thermostat/README.md) uses the same RCP firmware as `rcp_on_hosted_cp/*/cp`, with spinel on the dedicated UART (setup B). The host runs the Zigbee stack instead of OpenThread.

More: [OpenThread feature](../../docs/features/openthread.md).
