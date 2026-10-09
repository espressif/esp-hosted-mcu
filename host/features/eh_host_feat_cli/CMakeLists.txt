# SPDX-License-Identifier: Apache-2.0

set(srcs "src/eh_host_feat_cli.c")

if(CONFIG_ESP_HOSTED_HOST_FEAT_POWER_SAVE_READY)
    list(APPEND srcs "src/eh_host_feat_cli_host_ps.c")
endif()

if(CONFIG_ESP_HOSTED_HOST_FEAT_RF_CERT_READY)
    list(APPEND srcs "src/eh_host_feat_cli_rf_cert.c")
endif()

add_library(eh_host_feat_cli STATIC ${srcs})

target_include_directories(eh_host_feat_cli
    PUBLIC  "include"
    PRIVATE "src"
)

target_link_libraries(eh_host_feat_cli
    PUBLIC
        eh_host_compat_types
        eh_host_core
        idf::console
    PRIVATE
        eh_host_config
)

if(CONFIG_ESP_HOSTED_HOST_FEAT_POWER_SAVE_READY)
    target_link_libraries(eh_host_feat_cli PUBLIC eh_host_feat_power_save)
endif()

if(CONFIG_ESP_HOSTED_HOST_FEAT_RF_CERT_READY)
    target_link_libraries(eh_host_feat_cli PUBLIC eh_host_feat_rf_cert)
    # gpio_output_set drives a coprocessor pin, so it needs the GPIO expander.
    if(CONFIG_ESP_HOSTED_HOST_FEAT_GPIO_EXP_READY)
        target_link_libraries(eh_host_feat_cli PUBLIC eh_host_feat_gpio_exp)
    endif()
endif()

if(CONFIG_ESP_HOSTED_HOST_FEAT_CLI_AUTO_INIT)
    target_link_options(eh_host_feat_cli INTERFACE
        "-Wl,--undefined=eh_host_feat_cli_init")
endif()
