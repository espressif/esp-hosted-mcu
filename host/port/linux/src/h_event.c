/* host/port/linux/src/h_event.c
 * Linux mock Event port — linked-list handler registry. */

#include "h_port_contract.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct event_handler_entry {
    h_event_base_t base;
    int32_t event_id;
    h_event_handler_t handler;
    void *user_ctx;
    struct event_handler_entry *next;
} event_handler_entry_t;

static event_handler_entry_t *g_handlers = NULL;
static pthread_mutex_t g_event_lock = PTHREAD_MUTEX_INITIALIZER;

static int linux_event_register(h_event_base_t base, int32_t event_id,
                                h_event_handler_t handler, void *user_ctx)
{
    event_handler_entry_t *e = malloc(sizeof(*e));
    if (!e) return H_ERR_NO_MEM;
    e->base = base;
    e->event_id = event_id;
    e->handler = handler;
    e->user_ctx = user_ctx;

    pthread_mutex_lock(&g_event_lock);
    e->next = g_handlers;
    g_handlers = e;
    pthread_mutex_unlock(&g_event_lock);
    return H_OK;
}

static int linux_event_unregister(h_event_base_t base, int32_t event_id,
                                  h_event_handler_t handler)
{
    pthread_mutex_lock(&g_event_lock);
    event_handler_entry_t **pp = &g_handlers;
    while (*pp) {
        if ((*pp)->base == base && (*pp)->event_id == event_id &&
            (*pp)->handler == handler) {
            event_handler_entry_t *tmp = *pp;
            *pp = (*pp)->next;
            pthread_mutex_unlock(&g_event_lock);
            free(tmp);
            return H_OK;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_event_lock);
    return H_ERR_INVALID_ARG;
}

static int linux_event_post(h_event_base_t base, int32_t event_id,
                            void *event_data, size_t event_data_size)
{
    /* Note: callbacks are invoked under the lock to prevent concurrent
     * unregister from freeing a node during traversal. Callbacks should
     * not re-enter register/unregister or block for long periods. */
    pthread_mutex_lock(&g_event_lock);
    for (event_handler_entry_t *e = g_handlers; e; e = e->next) {
        if (e->base == base && e->event_id == event_id) {
            e->handler(event_data, event_data_size, e->user_ctx);
        }
    }
    pthread_mutex_unlock(&g_event_lock);
    return H_OK; /* No handler is not an error */
}

static int linux_event_wifi_post(int32_t event_id, void *event_data,
                                 size_t event_data_size, int32_t timeout_ms)
{
    (void)timeout_ms; /* Linux mock has no real timeout semantics */
    return linux_event_post(H_EVENT_WIFI, event_id, event_data, event_data_size);
}

const h_event_contract_t g_h_event = {
    .register_handler   = linux_event_register,
    .unregister_handler = linux_event_unregister,
    .post               = linux_event_post,
    .wifi_post          = linux_event_wifi_post,
};

/* ── Port Init/Deinit ── */
h_err_t h_port_event_init(void)
{
    /* Nothing to init — registry starts empty */
    return H_OK;
}

void h_port_event_deinit(void)
{
    /* Free any remaining registered handlers */
    pthread_mutex_lock(&g_event_lock);
    event_handler_entry_t *e = g_handlers;
    while (e) {
        event_handler_entry_t *tmp = e;
        e = e->next;
        free(tmp);
    }
    g_handlers = NULL;
    pthread_mutex_unlock(&g_event_lock);
}
