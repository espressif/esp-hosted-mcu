# SPDX-License-Identifier: Apache-2.0
#
# main.c shows the transport configuration; the other files are optional extras.
# REQUIRES esp_hosted pulls the host stack and the NimBLE hosted-HCI glue.
idf_component_register(
    SRCS "main.c"
         "events.c"
         "bus_monitor.c"
         "wifi_sta.c"
         "ble_nimble.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hosted
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash bt esp_timer)
