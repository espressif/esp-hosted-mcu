/* Stub for Linux mock build -- minimal OS abstraction types */
#ifndef PORT_ESP_HOSTED_HOST_OS_H
#define PORT_ESP_HOSTED_HOST_OS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Legacy handle typedefs -- mapped to portable h_types in real usage */
#define thread_handle_t    void*
#define queue_handle_t     void*
#define semaphore_handle_t void*
#define mutex_handle_t     void*
#define spinlock_handle_t  void*
#define gpio_port_handle_t void*

#define gpio_pin_state_t   int

#define HOSTED_BLOCK_MAX     (-1)
#define HOSTED_BLOCKING      (-1)
#define HOSTED_NON_BLOCKING  0

#define RPC_TASK_STACK_SIZE  (5*1024)
#define RPC_TASK_PRIO        23
#define DFLT_TASK_STACK_SIZE (5*1024)
#define DFLT_TASK_PRIO       23

#define H_GPIO_MODE_DEF_DISABLE  (0)
#define H_GPIO_MODE_DEF_INPUT    (1)
#define H_GPIO_MODE_DEF_OUTPUT   (2)
#define H_GPIO_MODE_DEF_OD       (4)

enum {
    H_GPIO_MODE_DISABLE = H_GPIO_MODE_DEF_DISABLE,
    H_GPIO_MODE_INPUT = H_GPIO_MODE_DEF_INPUT,
    H_GPIO_MODE_OUTPUT = H_GPIO_MODE_DEF_OUTPUT,
    H_GPIO_MODE_OUTPUT_OD = ((H_GPIO_MODE_DEF_OUTPUT) | (H_GPIO_MODE_DEF_OD)),
    H_GPIO_MODE_INPUT_OUTPUT_OD = ((H_GPIO_MODE_DEF_INPUT) | (H_GPIO_MODE_DEF_OUTPUT) | (H_GPIO_MODE_DEF_OD)),
    H_GPIO_MODE_INPUT_OUTPUT = ((H_GPIO_MODE_DEF_INPUT) | (H_GPIO_MODE_DEF_OUTPUT)),
};

#define H_GPIO_PULL_UP   (1)
#define H_GPIO_PULL_DOWN (0)

#define RET_OK       0
#define RET_FAIL     -1
#define RET_INVALID  -2
#define RET_FAIL_MEM -3
#define RET_FAIL4    -4
#define RET_FAIL_TIMEOUT -5

#define HOSTED_MEM_ALIGNMENT_4  4
#define HOSTED_MEM_ALIGNMENT_32 32
#define HOSTED_MEM_ALIGNMENT_64 64

enum hardware_type_e {
    HARDWARE_TYPE_ESP32,
    HARDWARE_TYPE_OTHER_ESP_CHIPSETS,
    HARDWARE_TYPE_INVALID,
};

#define MILLISEC_TO_SEC       1000
#define TICKS_PER_SEC(x)      (1000*(x))
#define SEC_TO_MILLISEC(x)    (1000*(x))
#define SEC_TO_MICROSEC(x)    (1000*1000*(x))
#define MILLISEC_TO_MICROSEC(x) (1000*(x))

#define MEM_DUMP(s)  /* noop */

/* Legacy vtable macros -- these should not be used in core code anymore,
 * but old headers still reference them. Provide minimal fallback. */
extern struct hosted_osal_funcs {
    void* (*_h_malloc)(size_t);
    void* (*_h_calloc)(size_t, size_t);
    void  (*_h_free)(void*);
    void* (*_h_memcpy)(void*, const void*, size_t);
    void* (*_h_memset)(void*, int, size_t);
} g_h_osal_legacy;

#define HOSTED_CREATE_HANDLE(tYPE, hANDLE) {                                   \
    hANDLE = (tYPE *)h_malloc(sizeof(tYPE));                                   \
    if (!hANDLE) {                                                             \
        return NULL;                                                           \
    }                                                                          \
}

#define HOSTED_FREE_HANDLE(handle) { \
    if (handle) { \
        h_free(handle); \
        handle = NULL; \
    } \
}

#define HOSTED_FREE(buff) if (buff) { h_free(buff); buff = NULL; }
#define HOSTED_CALLOC(struct_name, buff, nbytes, gotosym) do {    \
    buff = (struct_name *)h_calloc(1, nbytes);   \
    if (!buff) {                                                  \
        goto gotosym;                                             \
    }                                                             \
} while(0);

#define HOSTED_MALLOC(struct_name, buff, nbytes, gotosym) do {    \
    buff = (struct_name *)h_malloc(nbytes);      \
    if (!buff) {                                                  \
        goto gotosym;                                             \
    }                                                             \
} while(0);

struct serial_drv_handle_t;
struct timer_handle_t;

#endif
