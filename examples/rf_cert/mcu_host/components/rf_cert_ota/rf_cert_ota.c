/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Coprocessor OTA over the hosted link.
 *
 * The image rides in a LittleFS partition built from cp_fw_bin/, so the
 * example carries no firmware of its own and a different image needs only a
 * reflash. Activating reboots the coprocessor, so the host tears the
 * transport down and restarts to resync, as examples/ota does; keeping the
 * link up lets the next RPC hit a vanished slave, which the transport
 * reports as an unrecoverable write and answers by aborting.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_system.h"

#include "eh_host_core.h"
#include "eh_host_cp_ota.h"
#include "eh_host_sys.h"
#include "rf_cert_ota.h"

static const char *TAG = "rf_cert_ota";

#define CP_FW_PARTITION "cp_fw"
#define CP_FW_MOUNT     "/cp_fw"
#define CHUNK_SIZE      EH_HOST_CP_OTA_CHUNK_MAX

/* cp_fw_bin/ is required to hold exactly one .bin, so the first one found is
 * the one the user meant. */
static esp_err_t find_image(char *path, size_t path_len, size_t *size)
{
    DIR *d = opendir(CP_FW_MOUNT);
    struct dirent *e;
    esp_err_t rc = ESP_ERR_NOT_FOUND;

    if (!d) {
        return ESP_ERR_NOT_FOUND;
    }
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');

        if (!dot || strcmp(dot, ".bin") != 0) {
            continue;
        }
        snprintf(path, path_len, CP_FW_MOUNT "/%s", e->d_name);

        FILE *f = fopen(path, "rb");
        if (!f) {
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fclose(f);
        if (n <= 0) {
            continue;
        }
        *size = (size_t)n;
        rc = ESP_OK;
        break;
    }
    closedir(d);
    return rc;
}

#if CONFIG_RF_CERT_EXAMPLE_OTA_REPORT_VERSION
/* The app descriptor sits straight after the image header and the first
 * segment header, so the version is readable without parsing the whole
 * image. Returns false only if the file is too short to hold one. */
static bool staged_version(const char *path, esp_app_desc_t *out)
{
    FILE *f = fopen(path, "rb");
    size_t off = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    bool ok = false;

    if (!f) {
        return false;
    }
    if (fseek(f, (long)off, SEEK_SET) == 0 &&
        fread(out, 1, sizeof(*out), f) == sizeof(*out) &&
        out->magic_word == ESP_APP_DESC_MAGIC_WORD) {
        ok = true;
    }
    fclose(f);
    return ok;
}
#endif

static void how_to_stage(void)
{
    ESP_LOGW(TAG, "no coprocessor image staged in " CP_FW_PARTITION);
    printf("\n"
           "  1. Build the coprocessor app you want to end up running\n"
           "       <cp-project>/build/<project_name>.bin\n"
           "\n"
           "  2. Copy that .bin into the host project, one file only\n"
           "       examples/rf_cert/mcu_host/components/rf_cert_ota/cp_fw_bin/\n"
           "\n"
           "  3. Rebuild and flash the host\n"
           "\n"
           "  Then run phy_cert_cp_ota again.\n"
           "\n");
}

esp_err_t rf_cert_ota_perform(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = CP_FW_MOUNT,
        .partition_label        = CP_FW_PARTITION,
        .format_if_mount_failed = false,
        .dont_mount             = false,
    };
    char path[64];
    size_t total = 0, sent = 0;
    uint8_t *chunk = NULL;
    FILE *f = NULL;
    bool restart = false;
    esp_err_t rc;

    rc = esp_vfs_littlefs_register(&conf);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "mount %s: %s", CP_FW_PARTITION, esp_err_to_name(rc));
        return rc;
    }

    if (find_image(path, sizeof(path), &total) != ESP_OK) {
        how_to_stage();
        rc = ESP_ERR_NOT_FOUND;
        goto out;
    }

    f = fopen(path, "rb");
    chunk = malloc(CHUNK_SIZE);
    if (!f || !chunk) {
        ESP_LOGE(TAG, "cannot read %s", path);
        rc = ESP_ERR_NO_MEM;
        goto out;
    }

#if CONFIG_RF_CERT_EXAMPLE_OTA_REPORT_VERSION
    {
        eh_host_coprocessor_fwver_t cur = {0};
        esp_app_desc_t staged = {0};
        char cur_str[32] = "unknown";

        if (eh_host_sys_get_cp_fw_version(&cur) == ESP_OK) {
            snprintf(cur_str, sizeof(cur_str), "%u.%u.%u",
                     (unsigned)cur.major1, (unsigned)cur.minor1, (unsigned)cur.patch1);
        }
        if (staged_version(path, &staged)) {
            ESP_LOGI(TAG, "coprocessor runs %s; staged %s %s",
                     cur_str, staged.project_name, staged.version);
#if CONFIG_RF_CERT_EXAMPLE_OTA_SKIP_IF_SAME
            /* Only major.minor.patch can be compared: the coprocessor reports
             * those as numbers over RPC, while the image carries a free-form
             * string such as "v3.0.9-20-gabc1234".  Two builds of the same
             * version therefore count as the same firmware. */
            unsigned smaj = 0, smin = 0, spat = 0;
            const char *v = staged.version;

            if (*v == 'v' || *v == 'V') {
                v++;
            }
            if (sscanf(v, "%u.%u.%u", &smaj, &smin, &spat) == 3 &&
                smaj == cur.major1 && smin == cur.minor1 && spat == cur.patch1) {
                ESP_LOGW(TAG, "same version — nothing to do");
                ESP_LOGW(TAG, "turn off RF_CERT_EXAMPLE_OTA_SKIP_IF_SAME to send it anyway");
                rc = ESP_OK;
                goto out;
            }
#endif
        } else {
            ESP_LOGW(TAG, "coprocessor runs %s; staged image carries no app descriptor",
                     cur_str);
        }
    }
#endif

    ESP_LOGI(TAG, "streaming %s, %u bytes", path, (unsigned)total);

    rc = eh_host_cp_ota_begin();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin: %s", esp_err_to_name(rc));
        goto out;
    }

    while (sent < total) {
        size_t n = fread(chunk, 1, CHUNK_SIZE, f);

        if (n == 0) {
            ESP_LOGE(TAG, "short read at %u", (unsigned)sent);
            rc = ESP_FAIL;
            goto out;
        }
        rc = eh_host_cp_ota_write(chunk, n);
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "ota_write at %u: %s", (unsigned)sent, esp_err_to_name(rc));
            goto out;
        }
        sent += n;
        if ((sent % (64 * 1024)) < CHUNK_SIZE) {
            ESP_LOGI(TAG, "  %u / %u", (unsigned)sent, (unsigned)total);
        }
    }

    rc = eh_host_cp_ota_end();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "ota_end: %s", esp_err_to_name(rc));
        goto out;
    }
    rc = eh_host_cp_ota_activate();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "ota_activate: %s", esp_err_to_name(rc));
        goto out;
    }
    restart = true;

out:
    if (f) {
        fclose(f);
    }
    free(chunk);
    esp_vfs_littlefs_unregister(CP_FW_PARTITION);

    if (!restart) {
        return rc;
    }

    ESP_LOGW(TAG, "activated — the coprocessor reboots; the host restarts to resync");
    if (eh_host_deinit() != ESP_OK) {
        ESP_LOGW(TAG, "eh_host_deinit failed before restart");
    }
    /* The coprocessor reboots OTA_ACTIVATE_RESTART_TIMEOUT after activate and
     * comes up in pending-verify; BOOTLOADER_APP_ROLLBACK marks the new image
     * aborted if anything resets it before it confirms — including this
     * restart, which then loses the update silently. The budget is that timer
     * plus the coprocessor's boot, measured at ~600 ms. */
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}
