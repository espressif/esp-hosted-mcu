/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ESP_HOSTED_MISC_H__
#define __ESP_HOSTED_MISC_H__

#include <stdbool.h>
#include "esp_mac.h"

#define ESP_HOSTED_APP_DESC_MAGIC_WORD (0xABCD5432)  /*!< The magic word for the esp_hosted_app_desc structure */

/**
 * @brief Description about ESP-Hosted application on co-processor.
 *
 * @note Some fields may be empty if config to enable full app descriptor
 *       is not enabled on the co-processor
 */
typedef struct {
	uint32_t magic_word;        /*!< Magic word ESP_HOSTED_APP_DESC_MAGIC_WORD */
	uint32_t secure_version;    /*!< Secure version */
	uint32_t reserv1[2];        /*!< reserv1 */
	char version[32];           /*!< Application version */
	char project_name[32];      /*!< Project name */
	char time[16];              /*!< Compile time */
	char date[16];              /*!< Compile date*/
	char idf_ver[32];           /*!< Version IDF */
	uint8_t app_elf_sha256[32]; /*!< sha256 of elf file */
	uint16_t min_efuse_blk_rev_full; /*!< Minimal eFuse block revision supported by image, in format: major * 100 + minor */
	uint16_t max_efuse_blk_rev_full; /*!< Maximal eFuse block revision supported by image, in format: major * 100 + minor */
	uint8_t mmu_page_size;      /*!< MMU page size in log base 2 format */
	uint8_t reserv3[3];         /*!< reserv3 */
	uint32_t reserv2[18];       /*!< reserv2 */
} esp_hosted_app_desc_t;

/**
  * @brief  Initialise the BT Controller on the co-processor
  *
  * @return ESP_OK on success
  */
esp_err_t esp_hosted_bt_controller_init(void);

/**
  * @brief  Deinitialise the BT Controller on the co-processor
  *
  * @param  mem_release. Also releases memory used by controller. Once released, the BT controller cannot reuse the memory and cannot be initialised
  *
  * @return ESP_OK on success
  */
esp_err_t esp_hosted_bt_controller_deinit(bool mem_release);

/**
  * @brief  Enables the BT Controller on the co-processor. Call only after initialising the BT controller.
  *
  * @return ESP_OK on success
  */
esp_err_t esp_hosted_bt_controller_enable(void);

/**
  * @brief  Disables the BT Controller on the co-processor. Call before deinitialising the BT controller.
  *
  * @return ESP_OK on success
  */
esp_err_t esp_hosted_bt_controller_disable(void);

/**
  * @brief  Set custom MAC address of the interface.
  *
  * This function allows you to overwrite the MAC addresses of the
  * interfaces set by the base MAC address.
  *
  * @param  mac  MAC address, length: 6 bytes/8 bytes.
  *         length: 6 bytes for MAC-48
  *                 8 bytes for EUI-64(used for ESP_MAC_IEEE802154 type)
  * @param  mac_len  Length of the mac array
  * @param  type  Type of MAC address
  *
  * @return ESP_OK on success
  */
esp_err_t esp_hosted_iface_mac_addr_set(uint8_t *mac, size_t mac_len, esp_mac_type_t type);

/**
  * @brief  Read MAC address of the interface.
  *
  * @param  mac base MAC address, length: 6 bytes/8 bytes.
  *         length: 6 bytes for MAC-48
  *                 8 bytes for EUI-64(used for IEEE 802.15.4)
  * @param  mac_len  Length of the mac array
  * @param  type Type of MAC address
  *
  * @return ESP_OK on success
  */
esp_err_t esp_hosted_iface_mac_addr_get(uint8_t *mac, size_t mac_len, esp_mac_type_t type);

/**
  * @brief  Return the size of the MAC type in bytes.
  *
  * @param  type  Type of MAC address
  *
  * @return 0 MAC type not found (not supported)
  *         6 bytes for MAC-48.
  *         8 bytes for EUI-64.
  */
size_t esp_hosted_iface_mac_addr_len_get(esp_mac_type_t type);

/**
  * @brief  Return the co-processor application descriptor
  *
  * @param  app_desc  Pointer to Application Descriptor structure to fill
  *
  * @return ESP_OK on success
  */
esp_err_t esp_hosted_get_coprocessor_app_desc(esp_hosted_app_desc_t *app_desc);

/**
 * @brief Send custom data to co-processor
 *
 * @param msg_id    Message ID (any uint32_t except 0xFFFFFFFF which is reserved)
 * @param data      Data buffer to send
 * @param data_len  Length of data buffer
 *
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t esp_hosted_send_custom_data(uint32_t msg_id, const uint8_t *data, size_t data_len);

/**
 * @brief Register callback for custom data reception
 *
 * @param msg_id    Message ID to listen for (any uint32_t except 0xFFFFFFFF)
 * @param callback  Function called when data with this msg_id is received (NULL to deregister)
 *
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t esp_hosted_register_custom_callback(uint32_t msg_id,
    void (*callback)(uint32_t msg_id, const uint8_t *data, size_t data_len));

#endif
