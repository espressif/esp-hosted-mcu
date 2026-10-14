# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Which host image a bench boots.

Examples listed here run as a scenario of the test-owned super host
(tests/fw/host_super): each example's own main/ is linked in unchanged and the
bench picks it on the console at boot. EH_HOST_PROFILE=own forces every example's
own host app (bisect aid)."""
import os

from infra import lab

_SUPER = "host_super"
SELECT_PROMPT = "eh_super: select scenario"
_SCENARIO_OF = {
    ("system/api_exerciser", "mcu_host"): "api_exerciser",
    ("system/get_cp_fw_version", "mcu_host"): "get_cp_fw_version",
    ("system/hosted_events", "mcu_host"): "hosted_events",
    ("mem_monitor", "mcu_host"): "mem_monitor",
    ("peer_data_transfer", "mcu_host"): "peer_data_transfer",
    ("ext_coex", "mcu_host"): "ext_coex",
    ("wifi/sta", "mcu_host"): "wifi_sta",
    ("power_save/cp/shut_down_cp_when_unused", "mcu_host"): "shut_down_cp_when_unused",
    ("ota/coprocessor_ota", "mcu_host"): "ota",
    ("bluetooth/esp_hosted_nimble/bleprph_minimal", "mcu_host"): "bleprph_minimal",
    ("bluetooth/esp_hosted_nimble/bleprph_gatt", "mcu_host"): "bleprph_gatt",
    ("bluetooth/esp_hosted_nimble/bleprph_wifi_coex", "mcu_host"): "bleprph_wifi_coex",
    # Bluedroid family: IDF builds one BT host stack per image
    ("bluetooth/esp_hosted_bluedroid/ble_gatt_server", "mcu_host"): "bluedroid:ble_gatt_server",
    ("bluetooth/esp_hosted_bluedroid/ble_compatibility_test", "mcu_host"): "bluedroid:ble_compatibility_test",
    ("bluetooth/esp_hosted_bluedroid/controller_mac_addr", "mcu_host"): "bluedroid:controller_mac_addr",
    # rf_cert needs the host CLI, which only the diagnostics variant starts
    ("rf_cert", "mcu_host"): "diag:rf_cert",
}


def host_source(example: str, role: str):
    """(example_rel, role, scenario) for build_fw.build: the super host and the scenario
    to select when one serves this example, else the example's own app and None."""
    sc = _SCENARIO_OF.get((example, role))
    if not sc or os.environ.get("EH_HOST_PROFILE") == "own":
        return example, role, None
    super_role, _, sc = sc.rpartition(":")
    app_dir = os.path.join(str(lab._REPO), "tests", "fw", _SUPER)
    return (os.path.relpath(app_dir, os.path.join(str(lab._REPO), "examples")),
            super_role or "mcu_host", sc)


# Transport sub-variants the super host applies at runtime instead of a separate image.
_RUNTIME_OVL = {
    "CONFIG_ESP_HOSTED_HOST_SDIO_BUS_WIDTH_1=y": "sdio_width=1",
    "CONFIG_ESP_HOSTED_HOST_SPI_HD_1_DATA_LINE=y": "spi_hd_lines=1",
    "CONFIG_ESP_HOSTED_HOST_SPI_HD_2_DATA_LINES=y": "spi_hd_lines=2",
    "CONFIG_ESP_HOSTED_HOST_SPI_HD_4_DATA_LINES=y": "spi_hd_lines=4",
    "CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y": "log=debug",
}

# Instrumentation only the diagnostics variant (tests/fw/host_super/diag) builds in.
_DIAG_OVL = ("CONFIG_HEAP_POISONING_COMPREHENSIVE=y", "CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y")


def variant(role, overlay):
    """(role, overlay): an mcu_host scenario whose test asks for the diagnostics
    instrumentation runs on the diag variant, which has it built in."""
    if role == "mcu_host" and any(o in _DIAG_OVL for o in overlay):
        return "diag", [o for o in overlay if o not in _DIAG_OVL]
    return role, list(overlay)


# Lines every bench of an example gets, so its variants collapse into one image:
# the echo server is idle unless a test talks to it.
_ALWAYS_OVL = {
    ("network_split/iperf", "mcu_host"): ["CONFIG_EXAMPLE_NW_SPLIT_ECHO_SERVER=y",
                                          "CONFIG_EXAMPLE_NW_SPLIT_ECHO_PORT=49999"],
}


def always_overlay(example: str, role: str) -> list:
    return list(_ALWAYS_OVL.get((example, role), []))


def runtime_options(scenario, overlay):
    """(overlay, options): overlay lines the super host takes at runtime moved to options."""
    if not scenario:
        return list(overlay), []
    keep = [o for o in overlay if o not in _RUNTIME_OVL]
    return keep, [_RUNTIME_OVL[o] for o in overlay if _RUNTIME_OVL.get(o)]


def select_args(scenario, options=()):
    """esp-emu args that answer the super host's prompt with the scenario."""
    if not scenario:
        return []
    line = " ".join(["scenario", scenario, *options])
    return ["--inject-on", SELECT_PROMPT, "--inject", f"{line}\\n"]
