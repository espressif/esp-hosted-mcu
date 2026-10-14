# The example's LittleFS OTA without a build-time image: the bench writes the CP image into
# the storage partition at boot (build_fw.flash_with_fs); a blank one is formatted on mount.
idf_component_register(SRCS ex/ota_littlefs.c
                       INCLUDE_DIRS ex
                       PRIV_REQUIRES littlefs esp_hosted bootloader_support esp_app_format)
