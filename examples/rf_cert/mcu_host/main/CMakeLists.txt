# SPDX-License-Identifier: Apache-2.0
# rf_cert_ota is required unconditionally: component requirements are expanded
# before Kconfig, so they cannot vary with it.  The component is empty when
# CONFIG_RF_CERT_EXAMPLE_INTEGRATED_OTA is off.
idf_component_register(
    SRCS "host_rf_cert_example.c" "rf_cert_ota_cmd.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hosted rf_cert_ota
    PRIV_REQUIRES nvs_flash esp_event console)
