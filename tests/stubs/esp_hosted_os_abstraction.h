/* Stub for Linux mock build — h_transport_util.c includes this
 * but does not use any symbols from it (only needs h_wrapper.h).
 *
 * WP 4.2+: g_h is still referenced by h_transport_drv.c via legacy
 * MEMPOOL_ALLOC / MEMPOOL_FREE macros.  Provide minimal declaration
 * until h_transport_drv.c is fully decoupled in WP 4.4.
 */
#ifndef __ESP_HOSTED_OS_ABSTRACTION_H__
#define __ESP_HOSTED_OS_ABSTRACTION_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void*  (*_h_memcpy)(void* dest, const void* src, uint32_t size);
    void*  (*_h_memset)(void* buf, int val, size_t len);
    void*  (*_h_malloc)(size_t size);
    void*  (*_h_calloc)(size_t blk_no, size_t size);
    void   (*_h_free)(void* ptr);
    void*  (*_h_realloc)(void *mem, size_t newsize);
    void*  (*_h_malloc_align)(size_t size, size_t align);
    void   (*_h_free_align)(void* ptr);
    void*  (*_h_thread_create)(const char *tname, uint32_t tprio, uint32_t tstack_size, void (*start_routine)(void const *), void *sr_arg);
    int    (*_h_thread_cancel)(void *thread_handle);
    unsigned int (*_h_msleep)(unsigned int mseconds);
    unsigned int (*_h_usleep)(unsigned int useconds);
    unsigned int (*_h_sleep)(unsigned int seconds);
    unsigned int (*_h_blocking_delay)(unsigned int number);
    int    (*_h_queue_item)(void * queue_handle, void *item, int timeout);
    void*  (*_h_create_queue)(uint32_t qnum_elem, uint32_t qitem_size);
    int    (*_h_dequeue_item)(void * queue_handle, void *item, int timeout);
    int    (*_h_queue_msg_waiting)(void * queue_handle);
    int    (*_h_destroy_queue)(void * queue_handle);
    int    (*_h_reset_queue)(void * queue_handle);
    int    (*_h_unlock_mutex)(void * mutex_handle);
    void*  (*_h_create_mutex)(void);
    int    (*_h_lock_mutex)(void * mutex_handle, int timeout);
    int    (*_h_destroy_mutex)(void * mutex_handle);
    int    (*_h_post_semaphore)(void * semaphore_handle);
    int    (*_h_post_semaphore_from_isr)(void * semaphore_handle);
    void*  (*_h_create_semaphore)(int maxCount);
    int    (*_h_get_semaphore)(void * semaphore_handle, int timeout);
    int    (*_h_destroy_semaphore)(void * semaphore_handle);
    int    (*_h_timer_stop)(void *timer_handle);
    void*  (*_h_timer_start)(const char *name, int duration_ms, int type, void (*timeout_handler)(void *), void *arg);
    uint64_t (*_h_get_time_ms)(void);
#ifdef H_USE_MEMPOOL
    void*   (*_h_create_lock_mempool)(void);
    void   (*_h_lock_mempool)(void *lock_handle);
    void   (*_h_unlock_mempool)(void *lock_handle);
    void   (*_h_destroy_lock_mempool)(void *lock_handle);
#endif
    int (*_h_config_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t mode);
    int (*_h_config_gpio_as_interrupt)(void* gpio_port, uint32_t gpio_num, uint32_t intr_type, void (*gpio_isr_handler)(void* arg), void *arg);
    int (*_h_teardown_gpio_interrupt)(void* gpio_port, uint32_t gpio_num);
    int (*_h_read_gpio)(void* gpio_port, uint32_t gpio_num);
    int (*_h_write_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t value);
    int (*_h_pull_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t pull_value, uint32_t enable);
    int (*_h_hold_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t hold_value);
    int (*_h_get_host_wakeup_or_reboot_reason)(void);
    void * (*_h_bus_init)(void);
    int (*_h_bus_deinit)(void*);
#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI
    int (*_h_do_bus_transfer)(void *transfer_context);
#endif
    int (*_h_event_wifi_post)(int32_t event_id, void* event_data, size_t event_data_size, uint32_t ticks_to_wait);
    void (*_h_printf)(int level, const char *tag, const char *format, ...);
    void (*_h_hosted_init_hook)(void);
#if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO
    int (*_h_sdio_card_init)(void *ctx, bool show_config);
    int (*_h_sdio_card_deinit)(void*ctx);
    int (*_h_sdio_read_reg)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
    int (*_h_sdio_write_reg)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
    int (*_h_sdio_read_block)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
    int (*_h_sdio_write_block)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
    int (*_h_sdio_wait_slave_intr)(void *ctx, uint32_t ticks_to_wait);
#endif
#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD
    int (*_h_spi_hd_read_reg)(uint32_t reg, uint32_t *data, int poll, bool lock_required);
    int (*_h_spi_hd_write_reg)(uint32_t reg, uint32_t *data, bool lock_required);
    int (*_h_spi_hd_read_dma)(uint8_t *data, uint16_t size, bool lock_required);
    int (*_h_spi_hd_write_dma)(uint8_t *data, uint16_t size, bool lock_required);
    int (*_h_spi_hd_set_data_lines)(uint32_t data_lines);
    int (*_h_spi_hd_send_cmd9)(void);
#endif
#if H_TRANSPORT_IN_USE == H_TRANSPORT_UART
    int (*_h_uart_read)(void *ctx, uint8_t *data, uint16_t size);
    int (*_h_uart_write)(void *ctx, uint8_t *data, uint16_t size);
    int (*_h_uart_flush_input)(void *ctx);
#endif
    int (*_h_restart_host)(void);
    int (*_h_config_host_power_save_hal_impl)(uint32_t power_save_type, void* gpio_port, uint32_t gpio_num, int level);
    int (*_h_start_host_power_save_hal_impl)(uint32_t power_save_type);
    int (*_h_event_post)(void* event_base, int32_t event_id, void* event_data, size_t event_data_size, uint32_t ticks_to_wait);
} hosted_osi_funcs_t;

struct hosted_config_t {
    hosted_osi_funcs_t *funcs;
};

extern hosted_osi_funcs_t g_hosted_osi_funcs;
extern struct hosted_config_t g_h;

#endif
