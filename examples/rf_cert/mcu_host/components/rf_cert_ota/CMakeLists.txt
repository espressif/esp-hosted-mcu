# Coprocessor OTA for the RF cert example.
#
# Requirements must be identical in both CMake passes: IDF expands component
# requirements before Kconfig is read, so this file runs once with no CONFIG_
# values at all.  Only SRCS and the image step may depend on the Kconfig; the
# LittleFS requirement keys off whether the component manager actually fetched
# it, which is true in both passes.
set(srcs "")
if(CONFIG_RF_CERT_EXAMPLE_INTEGRATED_OTA)
    set(srcs "rf_cert_ota.c")
endif()

set(priv_reqs "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/joltwallet__littlefs")
    set(priv_reqs joltwallet__littlefs)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES esp_hosted
    PRIV_REQUIRES esp_app_format bootloader_support ${priv_reqs})

if(NOT CONFIG_RF_CERT_EXAMPLE_INTEGRATED_OTA)
    return()
endif()

# One .bin only, so it is unambiguous which image phy_cert_cp_ota sends.
# CONFIGURE_DEPENDS: without it, dropping a .bin in later changes nothing
# until the next clean configure.
file(GLOB cp_fw_bins CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/cp_fw_bin/*.bin")
list(LENGTH cp_fw_bins cp_fw_count)

if(cp_fw_count GREATER 1)
    message(FATAL_ERROR
        "cp_fw_bin/ holds ${cp_fw_count} .bin files; keep exactly one so it is "
        "unambiguous which image phy_cert_cp_ota sends.")
elseif(cp_fw_count EQUAL 1)
    message(STATUS "cp_fw: ${cp_fw_bins}")
else()
    message(STATUS
        "cp_fw: cp_fw_bin/ is empty — phy_cert_cp_ota will say so at run time.")
endif()

# A filesystem image is raw data, so esptool flashes it with the app and needs
# no --force, unlike a raw coprocessor image built for another chip.
littlefs_create_partition_image(cp_fw "${CMAKE_CURRENT_LIST_DIR}/cp_fw_bin" FLASH_IN_PROJECT)
