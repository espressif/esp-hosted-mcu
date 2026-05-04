/* tests/test_runner.c — Test runner entry point */
#include "unity.h"

/* Declare test suites */
extern void test_osal_malloc_free(void);
extern void test_osal_mutex_lock_unlock(void);
extern void test_osal_semaphore(void);
extern void test_osal_thread(void);
extern void test_osal_queue(void);
extern void test_osal_init_deinit_pair(void);
extern void test_vtable_null_protection(void);
extern void test_err_code_translation(void);
extern void test_rpc_request_timeout(void);
extern void test_rpc_request_response_match(void);
extern void test_event_register_and_post(void);
extern void test_event_multiple_handlers(void);
extern void test_transport_init_spi(void);
extern void test_transport_mock_transfer(void);

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    /* OSAL tests */
    RUN_TEST(test_osal_malloc_free);
    RUN_TEST(test_osal_mutex_lock_unlock);
    RUN_TEST(test_osal_semaphore);
    RUN_TEST(test_osal_thread);
    RUN_TEST(test_osal_queue);
    RUN_TEST(test_osal_init_deinit_pair);
    RUN_TEST(test_vtable_null_protection);
    RUN_TEST(test_err_code_translation);
    /* RPC core tests */
    RUN_TEST(test_rpc_request_timeout);
    RUN_TEST(test_rpc_request_response_match);
    /* Event tests */
    RUN_TEST(test_event_register_and_post);
    RUN_TEST(test_event_multiple_handlers);
    /* Transport tests */
    RUN_TEST(test_transport_init_spi);
    RUN_TEST(test_transport_mock_transfer);
    return UNITY_END();
}
