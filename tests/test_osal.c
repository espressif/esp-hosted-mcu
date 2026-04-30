/* tests/test_osal.c — OSAL unit tests (8 tests) */
#include "unity.h"
#include "h_port_contract.h"
#include "h_wrapper.h"
#include "h_types.h"
#include <string.h>

/* Test: malloc/free cycle */
void test_osal_malloc_free(void)
{
    void *p = h_malloc(128);
    TEST_ASSERT_NOT_NULL(p);
    h_free(p);
}

/* Test: mutex create/lock/unlock/delete */
void test_osal_mutex_lock_unlock(void)
{
    h_mutex_t m;
    TEST_ASSERT_EQUAL(H_OK, h_mutex_create(&m));
    TEST_ASSERT_EQUAL(H_OK, h_mutex_lock(m, 100));
    TEST_ASSERT_EQUAL(H_OK, h_mutex_unlock(m));
    TEST_ASSERT_EQUAL(H_OK, h_mutex_delete(m));
}

/* Test: semaphore give/take */
void test_osal_semaphore(void)
{
    h_semaphore_t s;
    TEST_ASSERT_EQUAL(H_OK, h_sem_create(10, 0, &s));
    TEST_ASSERT_EQUAL(H_OK, h_sem_give(s));
    TEST_ASSERT_EQUAL(H_OK, h_sem_take(s, 100));
    TEST_ASSERT_EQUAL(H_OK, h_sem_delete(s));
}

/* Test: thread create/delete */
static void dummy_thread(void *arg) { (void)arg; }

void test_osal_thread(void)
{
    h_thread_t t;
    TEST_ASSERT_EQUAL(H_OK, h_thread_create("test", 1, 4096,
                                             dummy_thread, NULL, &t));
    TEST_ASSERT_EQUAL(H_OK, h_thread_delete(t));
}

/* Test: queue send/recv */
void test_osal_queue(void)
{
    h_queue_t q;
    TEST_ASSERT_EQUAL(H_OK, h_queue_create(10, sizeof(int), &q));
    int val = 42;
    TEST_ASSERT_EQUAL(H_OK, h_queue_send(q, &val, 100));
    int recv = 0;
    TEST_ASSERT_EQUAL(H_OK, h_queue_recv(q, &recv, 100));
    TEST_ASSERT_EQUAL(42, recv);
    TEST_ASSERT_EQUAL(H_OK, h_queue_delete(q));
}

/* Test: init/deinit pair + idempotence */
void test_osal_init_deinit_pair(void)
{
    /* These call the Linux mock port entry points */
    extern h_err_t h_port_osal_init(void);
    extern void h_port_osal_deinit(void);
    /* First init/deinit cycle */
    TEST_ASSERT_EQUAL(H_OK, h_port_osal_init());
    h_port_osal_deinit();
    /* Second init — idempotent */
    TEST_ASSERT_EQUAL(H_OK, h_port_osal_init());
    h_port_osal_deinit();
}

/* Test: h_validate_contracts infrastructure */
void test_vtable_null_protection(void)
{
    /* h_validate_contracts() is defined in host/core/src/h_init.c
     * and checks that the three g_h_* vtable instances have their
     * required fields filled. */
    extern h_err_t h_validate_contracts(void);
    h_err_t ret = h_validate_contracts();
    TEST_ASSERT_EQUAL(H_OK, ret);
}

/* Test: error code semantics (POSIX errno convention) */
void test_err_code_translation(void)
{
    TEST_ASSERT_EQUAL(0, H_OK);
    TEST_ASSERT(H_ERR_INVALID_ARG < 0);
    TEST_ASSERT(H_ERR_NO_MEM < 0);
    TEST_ASSERT(H_ERR_TIMEOUT < 0);
    TEST_ASSERT(H_FAIL < 0);
    TEST_ASSERT(H_ERR_NOT_SUP != H_FAIL);
}
