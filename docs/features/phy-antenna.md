# PHY Antenna Selection

[Home](../../README.md) · [Getting Started: Linux](../getting-started-linux.md) · [Getting Started: MCU](../getting-started-mcu.md) · [Troubleshooting](../troubleshooting.md)

The host selects the co-processor antenna. The co-processor drives an external RF switch on up to four of its GPIOs.

```
host                         co-processor
eh_host_phy_ant_set_gpio() ─► esp_phy_set_ant_gpio() ─► ANT_SEL0..3 on CP GPIOs ─► RF switch
eh_host_phy_ant_set()      ─► esp_phy_set_ant()      ─► antenna 0 / 1 / AUTO
```

| Linux host | MCU host |
| :---: | :---: |
| Yes | Yes |

---

## Use

| Host (`eh_host_phy_ant.h`) | Co-processor (`esp_phy.h`) |
| :--- | :--- |
| `eh_host_phy_ant_set_gpio()` / `_get_gpio()` | `esp_phy_set_ant_gpio()` / `esp_phy_get_ant_gpio()` |
| `eh_host_phy_ant_set()` / `_get()` | `esp_phy_set_ant()` / `esp_phy_get_ant()` |

The calls behave as in ESP-IDF:

- GPIO numbers are co-processor GPIOs.
- A GPIO dropped from the configuration stays routed and reserved. Free it with `eh_host_cp_gpio_reset_pin()` ([GPIO Expander](gpio-expander.md)).
- The PHY drives `ANT_SEL` only while the radio is active.
- The configuration is lost when the co-processor resets. Set it again after a restart.

The co-processor refuses a GPIO that the transport uses or that is reserved, for example a flash pin. One refused GPIO refuses the whole request.

---

## Enable

`menuconfig` on both sides, off by default:

- host: `ESP_HOSTED_HOST_FEAT_PHY_ANT`
- co-processor: `ESP_HOSTED_CP_FEAT_PHY_ANT`

Without the co-processor feature, each host call returns `ESP_ERR_NOT_SUPPORTED`.

> [!NOTE]
> ESP-IDF v5.5 to v6.1 can refuse a free GPIO, for example GPIO 14 when GPIO 2 is reserved ([fix](https://gitlab.espressif.cn:6688/espressif/esp-idf/-/merge_requests/54665)). Without the fix, enable `ESP_HOSTED_CP_FEAT_PHY_ANT_IDF_GPIO_RSV_WORKAROUND` on the co-processor.

---

## See also

- [API exerciser](../../examples/system/api_exerciser/README.md): `phy_ant_set_gpio`, `phy_ant_get_gpio`, `phy_ant_set`, `phy_ant_get`
- [GPIO Expander](gpio-expander.md) · [API reference](../api-reference.md#phy-antenna-selection)
