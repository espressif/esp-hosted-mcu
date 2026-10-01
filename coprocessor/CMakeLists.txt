
if(NOT CONFIG_ESP_HOSTED_CP)
    return()
endif()

add_subdirectory(eh_cp_transport ${CMAKE_BINARY_DIR}/eh_cp_transport)

# Hosted BT needs the controller to itself, and _READY drops the feature
# silently when a BT host stack is built here. Fail instead of building a CP
# that drops every HCI frame from the host.
if(CONFIG_ESP_HOSTED_CP_FEAT_BT AND CONFIG_BT_ENABLED AND NOT CONFIG_BT_CONTROLLER_ONLY)
    message(FATAL_ERROR
        "ESP_HOSTED_CP_FEAT_BT is enabled, but a BT host stack is built on the coprocessor.\n"
        "Choose one:\n"
        "  1) Hosted BT: Component config -> Bluetooth -> Host -> Disabled (controller only).\n"
        "  2) Coprocessor app owns BT: set ESP_HOSTED_CP_FEAT_BT=n.")
endif()

foreach(pair
        rpc host_ps system nw_split cli peer_data
        rpc_ext_v1 rpc_ext_v2
        wifi wifi_ext_ent wifi_ext_itwt wifi_ext_dpp
        bt gpio_exp debug cp_ext_coex openthread)
    if(pair MATCHES "^([^:]+):(.+)$")
        set(_dir "${CMAKE_MATCH_1}")
        set(_cfg "${CMAKE_MATCH_2}")
    else()
        set(_dir "${pair}")
        set(_cfg "${pair}")
    endif()
    if(_dir STREQUAL "debug")
        add_subdirectory(features/eh_cp_feat_${_dir}
            ${CMAKE_BINARY_DIR}/eh_cp_feat_${_dir})
    else()
        string(TOUPPER "CONFIG_ESP_HOSTED_CP_FEAT_${_cfg}_READY" _eh_cfg)
        if(${_eh_cfg})
            add_subdirectory(features/eh_cp_feat_${_dir}
                ${CMAKE_BINARY_DIR}/eh_cp_feat_${_dir})
        endif()
    endif()
endforeach()

add_subdirectory(eh_cp_core ${CMAKE_BINARY_DIR}/eh_cp_core)

# eh_cp_feat_descs placement is handled by ldgen via coprocessor/eh_cp_feat_descs.lf
# (LDFRAGMENTS wired in the root CMakeLists) -> declared region, no linker warning.
