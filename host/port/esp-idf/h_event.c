/* host/port/esp-idf/h_event.c
 * ESP-IDF Event port — maps h_event_contract_t to esp_event API.
 *
 * The portable h_event_base_t enum (H_EVENT_WIFI, H_EVENT_IP) is translated
 * to ESP-IDF event base strings (WIFI_EVENT, IP_EVENT). Handler signatures
 * are compatible — both use void(*)(void*, size_t, void*). */

#include "h_port_contract.h"

#include <esp_event.h>

/* ──  Helpers ── */

/* Map h_err_t <-> esp_err_t */
static h_err_t esp_err_to_h_err(esp_err_t err)
{
    switch (err) {
        case ESP_OK:              return H_OK;
        case ESP_ERR_NO_MEM:      return H_ERR_NO_MEM;
        case ESP_ERR_INVALID_ARG: return H_ERR_INVALID_ARG;
        case ESP_ERR_TIMEOUT:     return H_ERR_TIMEOUT;
        default:                  return H_FAIL;
    }
}

/* Map portable event base to ESP-IDF event base string */
static esp_event_base_t h_base_to_esp(h_event_base_t base)
{
    switch (base) {
        case H_EVENT_WIFI:   return WIFI_EVENT;
        case H_EVENT_IP:     return IP_EVENT;
        case H_EVENT_HOSTED: return ESP_EVENT_ANY_BASE; /* best-effort */
        default:             return NULL;
    }
}

/* ──  Event Adapters ── */

static int h_event_register_adapter(h_event_base_t base, int32_t event_id,
                                    h_event_handler_t handler, void *user_ctx)
{
    esp_event_base_t esp_base = h_base_to_esp(base);
    if (!esp_base) return H_ERR_INVALID_ARG;

    esp_err_t ret = esp_event_handler_register(
        esp_base, event_id,
        (esp_event_handler_t)handler, user_ctx);

    return esp_err_to_h_err(ret);
}

static int h_event_unregister_adapter(h_event_base_t base, int32_t event_id,
                                      h_event_handler_t handler)
{
    esp_event_base_t esp_base = h_base_to_esp(base);
    if (!esp_base) return H_ERR_INVALID_ARG;

    esp_err_t ret = esp_event_handler_unregister(
        esp_base, event_id,
        (esp_event_handler_t)handler);

    return esp_err_to_h_err(ret);
}

static int h_event_post_adapter(h_event_base_t base, int32_t event_id,
                                void *event_data, size_t event_data_size)
{
    esp_event_base_t esp_base = h_base_to_esp(base);
    if (!esp_base) return H_ERR_INVALID_ARG;

    esp_err_t ret = esp_event_post(
        esp_base, event_id,
        event_data, event_data_size,
        portMAX_DELAY);

    return esp_err_to_h_err(ret);
}

/* ──  Global Event Contract Instance ── */

const h_event_contract_t g_h_event = {
    .register_handler   = h_event_register_adapter,
    .unregister_handler = h_event_unregister_adapter,
    .post               = h_event_post_adapter,
};
