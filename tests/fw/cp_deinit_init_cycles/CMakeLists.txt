# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
idf_component_register(SRCS "cp_deinit_init_cycles.c"
                       REQUIRES esp_hosted)
# Nothing calls in: pull the object for its EH_CP_FEAT_REGISTER descriptor.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u cp_cycles_spawn")
