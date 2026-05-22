/*
 * ESP-Hosted Host Port — Contract Definitions
 *
 * Three vtables define the complete set of platform capabilities the core
 * layer needs. A port implementation creates three const global instances
 * of these structs. The core layer accesses them exclusively through the
 * wrapper macros in h_wrapper.h.
 *
 * Each vtable is independently replaceable — swap OSAL for testing without
 * touching transport, or mock event layer without touching OSAL.
 */

#ifndef H_PORT_CONTRACT_H
#define H_PORT_CONTRACT_H

#include "h_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── OSAL Contract ── */
typedef struct {
    /* Memory (required) */
    void* (*malloc)(size_t size);
    void* (*calloc)(size_t n, size_t size);
    void* (*realloc)(void *mem, size_t newsize);
    void  (*free)(void *ptr);
    void* (*memcpy)(void *dst, const void *src, size_t n);
    void* (*memset)(void *s, int c, size_t n);
    void* (*malloc_align)(size_t size, size_t align);
    void  (*free_align)(void *ptr);

    /* Threads (required) */
    int (*thread_create)(const char *name, uint32_t prio, uint32_t stack,
                         void (*fn)(void*), void *arg, h_thread_t *out);
    int (*thread_delete)(h_thread_t thread);

    /* Mutex (required) */
    int (*mutex_create)(h_mutex_t *out);
    int (*mutex_lock)(h_mutex_t m, int32_t timeout_ms);
    int (*mutex_unlock)(h_mutex_t m);
    int (*mutex_delete)(h_mutex_t m);

    /* Queue (required) */
    int (*queue_create)(uint32_t count, uint32_t item_size, h_queue_t *out);
    int (*queue_send)(h_queue_t q, const void *item, int32_t timeout_ms);
    int (*queue_recv)(h_queue_t q, void *item, int32_t timeout_ms);
    int (*queue_msg_waiting)(h_queue_t q);
    int (*queue_reset)(h_queue_t q);
    int (*queue_delete)(h_queue_t q);

    /* Semaphore (required) */
    int (*sem_create)(uint32_t max, uint32_t init, h_semaphore_t *out);
    int (*sem_take)(h_semaphore_t sem, int32_t timeout_ms);
    int (*sem_give)(h_semaphore_t sem);
    int (*sem_give_from_isr)(h_semaphore_t sem, void *isr_ctx);
    int (*sem_delete)(h_semaphore_t sem);

    /* Critical Section (required) */
    void (*enter_critical)(void);
    void (*exit_critical)(void);

    /* Timer (required) */
    int (*timer_create)(const char *name, h_timer_t *out);
    int (*timer_start)(h_timer_t t, uint32_t period_ms, bool periodic,
                       void (*cb)(void*), void *arg);
    int (*timer_stop)(h_timer_t t);
    int (*timer_delete)(h_timer_t t);
    uint64_t (*get_time_ms)(void);

    /* Time / Delay (required) */
    void (*msleep)(uint32_t ms);
    void (*usleep)(uint32_t us);
    void (*blocking_delay)(unsigned int iterations);

    /* Logging (required) */
    void (*log_write)(int level, const char *tag, const char *fmt, ...);

    /* Platform-specific extensions (optional — port may leave NULL) */
    int  (*restart_host)(void);
    void (*hosted_init_hook)(void);
    int  (*woke_from_ps)(void);
    int  (*ps_init)(void);
    int  (*spi_hd_set_data_lines)(uint32_t data_lines);
} h_osal_contract_t;

/* ── Event Contract ── */
typedef struct {
    int (*register_handler)(h_event_base_t base, int32_t event_id,
                            h_event_handler_t handler, void *user_ctx);
    int (*unregister_handler)(h_event_base_t base, int32_t event_id,
                              h_event_handler_t handler);
    int (*post)(h_event_base_t base, int32_t event_id,
                void *event_data, size_t event_data_size);

    /* Convenience shortcut for Wi-Fi events (bridges to esp_event_post(WIFI_EVENT,…))
     * timeout_ms < 0  → block forever (maps to portMAX_DELAY on FreeRTOS)
     * timeout_ms >= 0 → milliseconds to wait                          */
    int (*wifi_post)(int32_t event_id, void *event_data,
                     size_t event_data_size, int32_t timeout_ms);
} h_event_contract_t;

/* ── Transport HAL Contract ──
 * One vtable covers all transport types. A port that only supports SPI
 * fills SPI fields and leaves SDIO/UART as NULL. The NULLs are guarded by
 * compile-time transport selection (#if H_TRANSPORT_IN_USE). */
typedef struct {
    /* Bus lifecycle */
    int (*init)(void **out_handle);
    int (*deinit)(void *handle);
    int (*bus_ready)(void *handle);

    /* Transmit packet */
    int (*transmit)(uint8_t if_type, uint8_t if_num,
                    uint8_t *payload, uint16_t len, uint8_t zcopy,
                    void *to_free, void (*free_fn)(void *), uint8_t flags);

    /* SPI Full-Duplex */
    int (*spi_transfer)(void *handle, void *transfer_ctx);

    /* SPI-HD — optional, only required when SPI-HD is selected.
     * Ports that do not support SPI-HD may leave all slots NULL.
     * The NULLs are guarded by compile-time transport selection
     * (#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD). */
    int (*spi_hd_read_reg)(void *handle, uint32_t reg, uint32_t *data,
                           int poll, bool lock);
    int (*spi_hd_write_reg)(void *handle, uint32_t reg, uint32_t *data,
                            bool lock);
    int (*spi_hd_read_dma)(void *handle, uint8_t *data, uint16_t size,
                           bool lock);
    int (*spi_hd_write_dma)(void *handle, uint8_t *data, uint16_t size,
                            bool lock);
    int (*spi_hd_send_cmd9)(void *handle);

    /* SDIO */
    int (*sdio_card_init)(void *handle, bool show_config);
    int (*sdio_read_reg)(void *handle, uint32_t reg, uint8_t *data,
                         uint16_t size, bool lock);
    int (*sdio_write_reg)(void *handle, uint32_t reg, uint8_t *data,
                          uint16_t size, bool lock);
    int (*sdio_read_block)(void *handle, uint32_t reg, uint8_t *data,
                           uint16_t size, bool lock);
    int (*sdio_write_block)(void *handle, uint32_t reg, uint8_t *data,
                            uint16_t size, bool lock);
    int (*sdio_wait_intr)(void *handle, uint32_t timeout_ms);

    /* UART */
    int (*uart_read)(void *handle, uint8_t *data, uint16_t size);
    int (*uart_write)(void *handle, uint8_t *data, uint16_t size);
    int (*uart_flush)(void *handle);

    /* GPIO */
    int (*gpio_config)(uint32_t pin, uint32_t mode);
    int (*gpio_set_intr)(uint32_t pin, uint32_t intr_type,
                         void (*isr)(void*), void *arg);
    int (*gpio_clear_intr)(uint32_t pin);
    int (*gpio_read)(uint32_t pin);
    int (*gpio_write)(uint32_t pin, uint32_t value);

    /* netif */
    int (*netif_create)(uint8_t if_type, uint8_t if_num);
    int (*netif_destroy)(uint8_t if_type, uint8_t if_num);
} h_transport_contract_t;

/* ── Global Contract Instances ──
 * Declared extern here; each port provides exactly one definition of each
 * in its own .c file. Core layer accesses via h_wrapper.h macros. */
extern const h_osal_contract_t      g_h_osal;
extern const h_event_contract_t     g_h_event;
extern const h_transport_contract_t g_h_transport;

#endif /* H_PORT_CONTRACT_H */
