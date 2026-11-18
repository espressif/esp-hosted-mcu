# SPDX-License-Identifier: Apache-2.0
# Master-config INTERFACE: include path of the master config header +
# eh_common shared types. Single attachment point for every host TU.

if(NOT TARGET eh_host_config)
    add_library(eh_host_config INTERFACE)
endif()

target_include_directories(eh_host_config INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include"
)

if(NOT ESP_PLATFORM)
    # Non-IDF: per-build sdkconfig.h dir from the Kconfig pipeline.
    target_include_directories(eh_host_config INTERFACE
        "${CMAKE_BINARY_DIR}/config"
    )
endif()

# Non-IDF: the Kconfig pipeline has no IDF to deliver sdkconfig.h. C/C++ only.
if(NOT ESP_PLATFORM)
    target_compile_options(eh_host_config INTERFACE
        "$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include sdkconfig.h>"
    )
endif()

# Bundle eh_common transitively.
if(TARGET eh_common)
    target_link_libraries(eh_host_config INTERFACE eh_common)
endif()
