
if(NOT CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT)
    return()
endif()

add_library(eh_cp_feat_nw_split STATIC
    "src/eh_cp_feat_nw_split.c"
)

target_include_directories(eh_cp_feat_nw_split
    PUBLIC
        "include"
        "."
)

set(reqs eh_cp_core eh_common)
set(priv_reqs idf::esp_timer idf::esp_wifi)

if(CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT_READY)
    list(APPEND reqs idf::esp_wifi idf::esp_netif idf::esp_event idf::lwip)
endif()

target_link_libraries(eh_cp_feat_nw_split
    PUBLIC
        ${reqs}
        ${priv_reqs}
)

if(CONFIG_ESP_HOSTED_CP_FEAT_HOST_PS_READY)
    target_link_libraries(eh_cp_feat_nw_split PUBLIC eh_cp_feat_host_ps)
endif()
if(CONFIG_ESP_HOSTED_CP_FEAT_WIFI_READY)
    target_link_libraries(eh_cp_feat_nw_split PUBLIC eh_cp_feat_wifi)
endif()

target_link_libraries(eh_cp_feat_nw_split INTERFACE
    -Wl,--wrap=esp_wifi_internal_reg_rxcb
)

# 1. Override lwIP's ephemeral port range for network split.
# 2. Force-include the shared header into all lwIP source files.
# 3. ESP_IDF_LWIP_HOOK_FILENAME cannot be used because some sources
#    (e.g. udp.c) do not include the hook header.
if(COMMAND idf_component_get_property)
    idf_component_get_property(eh_lwip lwip COMPONENT_LIB)
    if(TARGET ${eh_lwip})
        target_compile_options(${eh_lwip} PRIVATE "-include"
            "${CMAKE_CURRENT_LIST_DIR}/../../../common/eh_common/include/eh_nw_split_lwip_src_port_hook.h")
    endif()
endif()

if(CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT_AUTO_INIT)
    target_link_options(eh_cp_feat_nw_split INTERFACE
    "-Wl,--undefined=eh_cp_feat_nw_split_init")
endif()

if(TARGET eh_cp)
    target_link_libraries(eh_cp INTERFACE eh_cp_feat_nw_split)
endif()
