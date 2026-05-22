/* host/port/linux/src/h_osal.c
 * Linux mock OSAL — maps h_osal_contract_t to POSIX/pthread. */

#define _GNU_SOURCE
#include "h_port_contract.h"
#include "h_port_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <time.h>
#include <poll.h>
#include <errno.h>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
typedef struct {
    dispatch_semaphore_t sem;
} apple_sem_wrapper_t;
#endif

/* ── Thread ── */
typedef struct {
    pthread_t thread;
    void (*fn)(void*);
    void *arg;
} thread_wrapper_t;

static void *thread_entry(void *arg) {
    thread_wrapper_t *w = (thread_wrapper_t *)arg;
    w->fn(w->arg);

    return NULL;
}

static int linux_thread_create(const char *name, uint32_t prio, uint32_t stack,
                               void (*fn)(void*), void *arg, h_thread_t *out)
{
    (void)name; (void)prio; (void)stack;
    thread_wrapper_t *w = malloc(sizeof(*w));
    if (!w) return H_ERR_NO_MEM;
    w->fn = fn;
    w->arg = arg;
    int ret = pthread_create(&w->thread, NULL, thread_entry, w);
    if (ret != 0) {  return H_FAIL; }
    *out = (h_thread_t)w;
    return H_OK;
}

static int linux_thread_delete(h_thread_t thread)
{
    thread_wrapper_t *w = (thread_wrapper_t *)thread;
    if (!w) return H_ERR_INVALID_ARG;
    /* Cancel then join: production threads (rpc_rx/rpc_tx) are infinite loops
     * that only exit when the serial read returns error. In mock builds we
     * need a clean way to tear them down from rpc_core_deinit(). */
    pthread_cancel(w->thread);
    pthread_join(w->thread, NULL); free(w);
    /* w was freed in thread_entry */
    return H_OK;
}

/* ── Mutex ── */
static int linux_mutex_create(h_mutex_t *out)
{
    pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
    if (!m) return H_ERR_NO_MEM;
    pthread_mutex_init(m, NULL);
    *out = (h_mutex_t)m;
    return H_OK;
}

static int linux_mutex_lock(h_mutex_t m, int32_t timeout_ms)
{
    if (!m) return H_ERR_INVALID_ARG;
    if (timeout_ms < 0) {
        pthread_mutex_lock((pthread_mutex_t *)m);
        return H_OK;
    }
#if defined(__APPLE__)
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    uint64_t deadline_ms = (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL + (uint64_t)timeout_ms;
    while (1) {
        if (pthread_mutex_trylock((pthread_mutex_t *)m) == 0) {
            return H_OK;
        }
        clock_gettime(CLOCK_REALTIME, &now);
        uint64_t now_ms = (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
        if (now_ms >= deadline_ms) {
            return H_ERR_TIMEOUT;
        }
        usleep(1000);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    int ret = pthread_mutex_timedlock((pthread_mutex_t *)m, &ts);
    return (ret == 0) ? H_OK : H_ERR_TIMEOUT;
#endif
}

static int linux_mutex_unlock(h_mutex_t m)
{
    if (!m) return H_ERR_INVALID_ARG;
    return (pthread_mutex_unlock((pthread_mutex_t *)m) == 0) ? H_OK : H_FAIL;
}

static int linux_mutex_delete(h_mutex_t m)
{
    if (!m) return H_ERR_INVALID_ARG;
    pthread_mutex_destroy((pthread_mutex_t *)m);
    free(m);
    return H_OK;
}

/* ── Queue (pipe + mutex) ── */
typedef struct {
    int fd_read;
    int fd_write;
    uint32_t item_size;
    pthread_mutex_t lock;
} linux_queue_t;

static int linux_queue_create(uint32_t count, uint32_t item_size, h_queue_t *out)
{
    (void)count;
    linux_queue_t *q = malloc(sizeof(*q));
    if (!q) return H_ERR_NO_MEM;
    int fds[2];
    if (pipe(fds) != 0) { free(q); return H_FAIL; }
    q->fd_read = fds[0];
    q->fd_write = fds[1];
    q->item_size = item_size;
    pthread_mutex_init(&q->lock, NULL);
    *out = (h_queue_t)q;
    return H_OK;
}

static int linux_queue_send(h_queue_t q, const void *item, int32_t timeout_ms)
{
    if (!q || !item) return H_ERR_INVALID_ARG;
    linux_queue_t *lq = (linux_queue_t *)q;
    if (timeout_ms >= 0) {
        struct pollfd pfd;
        pfd.fd = lq->fd_write;
        pfd.events = POLLOUT;
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret == 0) return H_ERR_TIMEOUT;
        if (ret < 0) return H_FAIL;
    }
    pthread_mutex_lock(&lq->lock);
    ssize_t ret = write(lq->fd_write, item, lq->item_size);
    pthread_mutex_unlock(&lq->lock);
    return (ret == (ssize_t)lq->item_size) ? H_OK : H_FAIL;
}

static int linux_queue_recv(h_queue_t q, void *item, int32_t timeout_ms)
{
    if (!q || !item) return H_ERR_INVALID_ARG;
    linux_queue_t *lq = (linux_queue_t *)q;
    if (timeout_ms >= 0) {
        struct pollfd pfd;
        pfd.fd = lq->fd_read;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret == 0) return H_ERR_TIMEOUT;
        if (ret < 0) return H_FAIL;
    }
    ssize_t ret = read(lq->fd_read, item, lq->item_size);
    return (ret == (ssize_t)lq->item_size) ? H_OK : H_FAIL;
}

static int linux_queue_msg_waiting(h_queue_t q)
{
    /* pipe doesn't support querying pending count — return 1 as heuristic */
    (void)q;
    return 1;
}

static int linux_queue_reset(h_queue_t q)
{
    if (!q) return H_ERR_INVALID_ARG;
    linux_queue_t *lq = (linux_queue_t *)q;

    /* Set non-blocking to avoid hang on empty pipe */
    int flags = fcntl(lq->fd_read, F_GETFL, 0);
    fcntl(lq->fd_read, F_SETFL, flags | O_NONBLOCK);

    char buf[256];
    while (read(lq->fd_read, buf, sizeof(buf)) > 0) {
        /* drain */
    }

    /* Restore original flags */
    fcntl(lq->fd_read, F_SETFL, flags);
    return H_OK;
}

static int linux_queue_delete(h_queue_t q)
{
    if (!q) return H_ERR_INVALID_ARG;
    linux_queue_t *lq = (linux_queue_t *)q;
    close(lq->fd_read);
    close(lq->fd_write);
    pthread_mutex_destroy(&lq->lock);
    free(lq);
    return H_OK;
}

/* ── Semaphore ── */
static int linux_sem_create(uint32_t max, uint32_t init, h_semaphore_t *out)
{
    (void)max;
#if defined(__APPLE__)
    apple_sem_wrapper_t *s = malloc(sizeof(*s));
    if (!s) return H_ERR_NO_MEM;
    s->sem = dispatch_semaphore_create((long)init);
    if (!s->sem) {
        free(s);
        return H_FAIL;
    }
    *out = (h_semaphore_t)s;
    return H_OK;
#else
    sem_t *s = malloc(sizeof(sem_t));
    if (!s) return H_ERR_NO_MEM;
    sem_init(s, 0, init);
    *out = (h_semaphore_t)s;
    return H_OK;
#endif
}

static int linux_sem_take(h_semaphore_t sem, int32_t timeout_ms)
{
    if (!sem) return H_ERR_INVALID_ARG;
#if defined(__APPLE__)
    apple_sem_wrapper_t *wrapper = (apple_sem_wrapper_t *)sem;
    if (timeout_ms < 0) {
        return (dispatch_semaphore_wait(wrapper->sem, DISPATCH_TIME_FOREVER) == 0) ? H_OK : H_FAIL;
    }
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ms * 1000000LL);
    return (dispatch_semaphore_wait(wrapper->sem, deadline) == 0) ? H_OK : H_ERR_TIMEOUT;
#else
    if (timeout_ms < 0) {
        sem_wait((sem_t *)sem);
        return H_OK;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    return (sem_timedwait((sem_t *)sem, &ts) == 0) ? H_OK : H_ERR_TIMEOUT;
#endif
}

static int linux_sem_give(h_semaphore_t sem)
{
    if (!sem) return H_ERR_INVALID_ARG;
#if defined(__APPLE__)
    apple_sem_wrapper_t *wrapper = (apple_sem_wrapper_t *)sem;
    dispatch_semaphore_signal(wrapper->sem);
    return H_OK;
#else
    return (sem_post((sem_t *)sem) == 0) ? H_OK : H_FAIL;
#endif
}

static int linux_sem_give_from_isr(h_semaphore_t sem, void *isr_ctx)
{
    (void)isr_ctx;
    return linux_sem_give(sem);  /* Linux mock has no ISR context */
}

static int linux_sem_delete(h_semaphore_t sem)
{
    if (!sem) return H_ERR_INVALID_ARG;
#if defined(__APPLE__)
    apple_sem_wrapper_t *wrapper = (apple_sem_wrapper_t *)sem;
#if !OS_OBJECT_USE_OBJC
    dispatch_release(wrapper->sem);
#endif
    free(wrapper);
#else
    sem_destroy((sem_t *)sem);
    free(sem);
#endif
    return H_OK;
}

/* ── Critical Section ── */
static void linux_enter_critical(void) { /* no-op on mock */ }
static void linux_exit_critical(void)  { /* no-op on mock */ }

/* ── Timer ── */
static int linux_timer_create(const char *name, h_timer_t *out)
{
    (void)name;
    *out = NULL;
    return H_ERR_NOT_SUP; /* Stub for Phase 1 */
}

static int linux_timer_start(h_timer_t t, uint32_t period_ms, bool periodic,
                             void (*cb)(void*), void *arg)
{
    (void)t; (void)period_ms; (void)periodic; (void)cb; (void)arg;
    return H_ERR_NOT_SUP;
}

static int linux_timer_stop(h_timer_t t)
{
    (void)t;
    return H_ERR_NOT_SUP;
}

static int linux_timer_delete(h_timer_t t)
{
    (void)t;
    return H_ERR_NOT_SUP;
}

/* ── Time ── */
static uint64_t linux_get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL);
}

static void linux_msleep(uint32_t ms)   { usleep(ms * 1000); }

static void linux_usleep(uint32_t us)   { usleep(us); }

static void linux_blocking_delay(unsigned int n) { usleep(n); }

/* ── Log ── */
static void linux_log_write(int level, const char *tag, const char *fmt, ...)
{
    const char *level_str[] = {"NONE","ERR","WARN","INFO","DBG","VERB"};
    fprintf(stderr, "[%s] %s: ", level_str[level], tag);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ── Debug task stub (noop in mock build) ── */
void create_debugging_tasks(void)
{
    /* No debug tasks in Linux mock build */
}

/* ── Aligned Memory (Linux mock: no DMA alignment needed) ── */
static void *linux_malloc_align(size_t size, size_t align)
{
    (void)align;
    return malloc(size);
}

static void linux_free_align(void *ptr)
{
    free(ptr);
}

/* ── OSAL Vtable ── */
const h_osal_contract_t g_h_osal = {
    .malloc       = malloc,
    .calloc       = calloc,
    .realloc      = realloc,
    .free         = free,
    .memcpy       = memcpy,
    .memset       = memset,
    .malloc_align = linux_malloc_align,
    .free_align   = linux_free_align,

    .thread_create     = linux_thread_create,
    .thread_delete     = linux_thread_delete,
    .mutex_create      = linux_mutex_create,
    .mutex_lock        = linux_mutex_lock,
    .mutex_unlock      = linux_mutex_unlock,
    .mutex_delete      = linux_mutex_delete,

    .queue_create      = linux_queue_create,
    .queue_send        = linux_queue_send,
    .queue_recv        = linux_queue_recv,
    .queue_msg_waiting = linux_queue_msg_waiting,
    .queue_reset       = linux_queue_reset,
    .queue_delete      = linux_queue_delete,

    .sem_create        = linux_sem_create,
    .sem_take          = linux_sem_take,
    .sem_give          = linux_sem_give,
    .sem_give_from_isr = linux_sem_give_from_isr,
    .sem_delete        = linux_sem_delete,

    .enter_critical    = linux_enter_critical,
    .exit_critical     = linux_exit_critical,

    .timer_create  = linux_timer_create,
    .timer_start   = linux_timer_start,
    .timer_stop    = linux_timer_stop,
    .timer_delete  = linux_timer_delete,
    .get_time_ms   = linux_get_time_ms,

    .msleep         = linux_msleep,
    .usleep         = linux_usleep,
    .blocking_delay = linux_blocking_delay,

    .log_write      = linux_log_write,

    .restart_host   = NULL,
    .hosted_init_hook = NULL,
    .woke_from_ps   = NULL,
    .ps_init        = NULL,
    .spi_hd_set_data_lines = NULL,
};

/* ── Port Init/Deinit Entry Points ── */
h_err_t h_port_osal_init(void)
{
    /* Linux mock: OS is already up, nothing to initialize */
    return H_OK;
}

void h_port_osal_deinit(void)
{
    /* nothing to tear down */
}
