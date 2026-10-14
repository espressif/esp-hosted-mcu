# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Which CP image a bench boots.

Most example CPs differ only in sdkconfig, so one test-owned superset image per
transport replaces them; a test's own cp_overlay still layers on top. Examples whose
CP mode conflicts (or whose test checks its own CP code) keep their own CP.
EH_CP_PROFILE=own forces every example's own CP (bisect aid)."""
import os

from infra import lab

_FW = "tests/fw"
_APP_OF = {
    # cp_full: superset CP (Wi-Fi, BT controller, GPIO exp, ext coex, peer data, ...)
    "system/hosted_events": "cp_full",
    "system/mcu_transport_config": "cp_full",
    "system/get_cp_fw_version": "cp_full",
    "system/api_exerciser": "cp_full",
    "mem_monitor": "cp_full",
    "wifi/sta": "cp_full",
    "wifi/scan": "cp_full",
    "ota/coprocessor_ota": "cp_full",
    "ext_coex": "cp_full",
    "peer_data_transfer": "cp_full",
    "bluetooth/esp_hosted_nimble/bleprph_minimal": "cp_full",
    "bluetooth/esp_hosted_nimble/bleprph_gatt": "cp_full",
    "bluetooth/esp_hosted_nimble/bleprph_wifi_coex": "cp_full",
    "bluetooth/esp_hosted_custom/hci_demo": "cp_full",
    "bluetooth/esp_hosted_bluedroid/ble_gatt_server": "cp_full",
    "bluetooth/esp_hosted_bluedroid/ble_compatibility_test": "cp_full",
    "bluetooth/esp_hosted_bluedroid/controller_mac_addr": "cp_full",
    # cp_nwsplit: network split owns the network stack on the CP (+ host power save)
    "network_split/iperf": "cp_nwsplit",
    "network_split/station": "cp_nwsplit",
    "power_save/host+cp/network_split__host_deep_sleep_cp_light_sleep": "cp_nwsplit",
    "rf_cert": "cp_full",
    "test/init_race": "cp_full",
    # same sdkconfig and main as the OpenThread RCP: only project() differs
    "zigbee/thermostat": "example:openthread/cli",
}


def cp_source(example: str, cp_role: str = "cp", cp_app: str = None):
    """(example_rel, role) for build_fw.build: the test-owned CP app if one serves this
    example, else the example's own CP. cp_app='own' or EH_CP_PROFILE=own opts out."""
    app = cp_app or _APP_OF.get(example)
    if not app or app == "own" or os.environ.get("EH_CP_PROFILE") == "own":
        return example, cp_role
    if app.startswith("example:"):
        return app[len("example:"):], cp_role
    app_dir = os.path.join(str(lab._REPO), _FW, app)
    return os.path.relpath(app_dir, os.path.join(str(lab._REPO), "examples")), "cp"
