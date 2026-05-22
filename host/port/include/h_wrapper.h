/*
 * ESP-Hosted Host Port — Wrapper Macros
 *
 * Core layer code writes h_malloc(sz), not g_h_osal.malloc(sz).
 * The macros provide:
 *   - Shorter, readable names
 *   - Single indirection (2 CPU cycles on Cortex-M4, negligible)
 *   - Replaceable globals for testing (swap g_h_osal per test suite)
 */

#ifndef H_WRAPPER_H
#define H_WRAPPER_H

#include "h_port_contract.h"

/* ── OSAL ── */
#define h_malloc(sz)                    (g_h_osal.malloc(sz))
#define h_calloc(n, sz)                 (g_h_osal.calloc(n, sz))
#define h_realloc(m, ns)                (g_h_osal.realloc(m, ns))
#define h_free(p)                       (g_h_osal.free(p))
#define h_memcpy(d, s, n)               (g_h_osal.memcpy(d, s, n))
#define h_memset(p, c, n)               (g_h_osal.memset(p, c, n))
#define h_malloc_align(sz, al)          (g_h_osal.malloc_align(sz, al))
#define h_free_align(p)                 (g_h_osal.free_align(p))

/* Raw function pointers for callback slots and config structs. */
#define h_free_fn                       (g_h_osal.free)
#define h_memset_fn                     (g_h_osal.memset)

#define h_thread_create(n,pr,st,fn,a,o) (g_h_osal.thread_create(n,pr,st,fn,a,o))
#define h_thread_delete(t)              (g_h_osal.thread_delete(t))

#define h_mutex_create(o)               (g_h_osal.mutex_create(o))
#define h_mutex_lock(m, to)             (g_h_osal.mutex_lock(m, to))
#define h_mutex_unlock(m)               (g_h_osal.mutex_unlock(m))
#define h_mutex_delete(m)               (g_h_osal.mutex_delete(m))

#define h_queue_create(c, s, o)         (g_h_osal.queue_create(c, s, o))
#define h_queue_send(q, i, to)          (g_h_osal.queue_send(q, i, to))
#define h_queue_recv(q, i, to)          (g_h_osal.queue_recv(q, i, to))
#define h_queue_msg_waiting(q)          (g_h_osal.queue_msg_waiting(q))
#define h_queue_reset(q)                (g_h_osal.queue_reset(q))
#define h_queue_delete(q)               (g_h_osal.queue_delete(q))

#define h_sem_create(mx, in, o)         (g_h_osal.sem_create(mx, in, o))
#define h_sem_take(s, to)               (g_h_osal.sem_take(s, to))
#define h_sem_give(s)                   (g_h_osal.sem_give(s))
#define h_sem_give_from_isr(s, ctx)     (g_h_osal.sem_give_from_isr(s, ctx))
#define h_sem_delete(s)                 (g_h_osal.sem_delete(s))

#define h_enter_critical()              (g_h_osal.enter_critical())
#define h_exit_critical()               (g_h_osal.exit_critical())

#define h_timer_create(n, o)            (g_h_osal.timer_create(n, o))
#define h_timer_start(t, ms, p, cb, a)  (g_h_osal.timer_start(t, ms, p, cb, a))
#define h_timer_stop(t)                 (g_h_osal.timer_stop(t))
#define h_timer_delete(t)               (g_h_osal.timer_delete(t))
#define h_get_time_ms()                 (g_h_osal.get_time_ms())

#define h_msleep(ms)                    (g_h_osal.msleep(ms))
#define h_usleep(us)                    (g_h_osal.usleep(us))
#define h_blocking_delay(n)             (g_h_osal.blocking_delay(n))

/* ── Blocking Semantics ── */
#ifndef H_BLOCK_FOREVER
#define H_BLOCK_FOREVER                 (-1)
#endif
#ifndef H_BLOCK_MAX
#define H_BLOCK_MAX                     H_BLOCK_FOREVER
#endif

/* ── Log Tag ── */
#ifndef DEFINE_LOG_TAG
#define DEFINE_LOG_TAG(sTr) static const char TAG[] = #sTr
#endif

/* ── Memory Alignment ── */
#ifndef HOSTED_MEM_ALIGNMENT_4
#define HOSTED_MEM_ALIGNMENT_4  4
#endif
#ifndef HOSTED_MEM_ALIGNMENT_32
#define HOSTED_MEM_ALIGNMENT_32 32
#endif
#ifndef HOSTED_MEM_ALIGNMENT_64
#define HOSTED_MEM_ALIGNMENT_64 64
#endif

/* ── Log ── */
typedef enum {
    H_LOG_NONE = 0,
    H_LOG_ERROR,
    H_LOG_WARN,
    H_LOG_INFO,
    H_LOG_DEBUG,
    H_LOG_VERBOSE
} h_log_level_t;

#define H_LOGE(tag, fmt, ...)  g_h_osal.log_write(H_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define H_LOGW(tag, fmt, ...)  g_h_osal.log_write(H_LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#define H_LOGI(tag, fmt, ...)  g_h_osal.log_write(H_LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define H_LOGD(tag, fmt, ...)  g_h_osal.log_write(H_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define H_LOGV(tag, fmt, ...)  g_h_osal.log_write(H_LOG_VERBOSE, tag, fmt, ##__VA_ARGS__)

#ifndef H_HEXLOGD
#define H_HEXLOGD(tag, data, len, width)                                \
    do {                                                                \
        (void)(tag);                                                    \
        (void)(data);                                                   \
        (void)(len);                                                    \
        (void)(width);                                                  \
    } while (0)
#endif

/* ── Event ── */
#define h_event_register(b, i, cb, ctx)  (g_h_event.register_handler(b, i, cb, ctx))
#define h_event_unregister(b, i, cb)     (g_h_event.unregister_handler(b, i, cb))
#define h_event_post(b, i, d, l)         (g_h_event.post(b, i, d, l))
#define h_event_wifi_post(i, d, l, t)    (g_h_event.wifi_post(i, d, l, t))

/* ── Transport ── */
#define h_transport_init(o)              (g_h_transport.init(o))
#define h_transport_deinit(h)            (g_h_transport.deinit(h))

#define h_spi_transfer(h, ctx)           (g_h_transport.spi_transfer(h, ctx))

/* SPI-HD — optional transport extension, only present when SPI-HD selected */
#define h_spi_hd_read_reg(h, r, d, p, l)    (g_h_transport.spi_hd_read_reg(h, r, d, p, l))
#define h_spi_hd_write_reg(h, r, d, l)      (g_h_transport.spi_hd_write_reg(h, r, d, l))
#define h_spi_hd_read_dma(h, d, s, l)       (g_h_transport.spi_hd_read_dma(h, d, s, l))
#define h_spi_hd_write_dma(h, d, s, l)      (g_h_transport.spi_hd_write_dma(h, d, s, l))
#define h_spi_hd_send_cmd9(h)               (g_h_transport.spi_hd_send_cmd9(h))

#define h_sdio_read_block(h,r,d,s,l)     (g_h_transport.sdio_read_block(h,r,d,s,l))
#define h_sdio_write_block(h,r,d,s,l)    (g_h_transport.sdio_write_block(h,r,d,s,l))
#define h_sdio_wait_intr(h, to)          (g_h_transport.sdio_wait_intr(h, to))

#define h_bus_ready(h)                   (g_h_transport.bus_ready(h))
#define h_transmit(t, n, p, l, z, f, fn, fl) \
    (g_h_transport.transmit(t, n, p, l, z, f, fn, fl))

#define h_uart_read(h, d, s)             (g_h_transport.uart_read(h, d, s))
#define h_uart_write(h, d, s)            (g_h_transport.uart_write(h, d, s))

#define h_gpio_config(pin, mode)         (g_h_transport.gpio_config(pin, mode))
#define h_gpio_set_intr(pin, t, isr, a)  (g_h_transport.gpio_set_intr(pin, t, isr, a))
#define h_gpio_clear_intr(pin)           (g_h_transport.gpio_clear_intr(pin))
#define h_gpio_read(pin)                 (g_h_transport.gpio_read(pin))
#define h_gpio_write(pin, val)           (g_h_transport.gpio_write(pin, val))

#define h_netif_create(t, n)             (g_h_transport.netif_create(t, n))
#define h_netif_destroy(t, n)            (g_h_transport.netif_destroy(t, n))

/* ── Platform-specific extensions (optional in h_osal_contract_t) ── */
#define h_restart_host() \
    H_VTABLE_CALL(&g_h_osal, restart_host)

#define h_hosted_init_hook() \
    do { if (g_h_osal.hosted_init_hook) g_h_osal.hosted_init_hook(); } while(0)

#define h_woke_from_ps() \
    (g_h_osal.woke_from_ps ? g_h_osal.woke_from_ps() : 0)

#define h_ps_init() \
    (g_h_osal.ps_init ? g_h_osal.ps_init() : H_OK)

#define h_spi_hd_set_data_lines(n) \
    H_VTABLE_CALL(&g_h_osal, spi_hd_set_data_lines, n)

/* ── Safe Callback (for optional vtable functions) ── */
#define H_VTABLE_CALL(ops, func, ...) \
    ((ops)->func ? (ops)->func(__VA_ARGS__) : H_ERR_NOT_SUP)

#endif /* H_WRAPPER_H */
