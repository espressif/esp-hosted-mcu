# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
idf_component_register(SRCS "cp_nw_split_lease.c"
                       REQUIRES esp_hosted esp_netif esp_event esp_wifi console)
# Nothing calls in: pull the object for its EH_CP_FEAT_REGISTER descriptor.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u cp_nw_split_lease_init")
