# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
# WHOLE_ARCHIVE: nothing references this component, so without it the linker
# drops the object and the EH_CP_FEAT_REGISTER descriptor never lands.
idf_component_register(SRCS "cp_deinit_init_cycles.c"
                       REQUIRES esp_hosted
                       WHOLE_ARCHIVE)
