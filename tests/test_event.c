/* tests/test_event.c — Event layer unit tests (2 tests) */
#include "unity.h"
#include "h_event.h"
#include "h_types.h"
#include "h_wrapper.h"

static int g_event_fired = 0;

static void test_handler(void *data, size_t len, void *ctx)
{
    (void)data; (void)len;
    g_event_fired = *(int *)ctx;
}

static int g_handler_count = 0;

static void counting_handler(void *data, size_t len, void *ctx)
{
    (void)data; (void)len; (void)ctx;
    g_handler_count++;
}

void test_event_register_and_post(void)
{
    g_event_fired = 0;
    int ctx = 42;
    h_err_t ret = h_event_register(H_EVENT_WIFI, H_EVENT_WIFI_READY,
                                   test_handler, &ctx);
    TEST_ASSERT_EQUAL(H_OK, ret);

    ret = h_event_post(H_EVENT_WIFI, H_EVENT_WIFI_READY, NULL, 0);
    TEST_ASSERT_EQUAL(H_OK, ret);
    TEST_ASSERT_EQUAL(42, g_event_fired);

    ret = h_event_unregister(H_EVENT_WIFI, H_EVENT_WIFI_READY, test_handler);
    TEST_ASSERT_EQUAL(H_OK, ret);
}

void test_event_multiple_handlers(void)
{
    g_handler_count = 0;

    /* Register 3 handlers for the same event */
    TEST_ASSERT_EQUAL(H_OK, h_event_register(H_EVENT_IP, 99,
                          counting_handler, NULL));
    TEST_ASSERT_EQUAL(H_OK, h_event_register(H_EVENT_IP, 99,
                          counting_handler, NULL));
    TEST_ASSERT_EQUAL(H_OK, h_event_register(H_EVENT_IP, 99,
                          counting_handler, NULL));

    /* Post — all 3 handlers should fire */
    h_event_post(H_EVENT_IP, 99, NULL, 0);
    TEST_ASSERT_EQUAL(3, g_handler_count);

    /* Unregister all */
    h_event_unregister(H_EVENT_IP, 99, counting_handler);
    h_event_unregister(H_EVENT_IP, 99, counting_handler);
    h_event_unregister(H_EVENT_IP, 99, counting_handler);
}
