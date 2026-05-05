/* tests/test_transport.c — Transport layer unit tests (2 tests) */
#include "unity.h"
#include "h_port_contract.h"
#include "h_wrapper.h"
#include "h_types.h"
#include "h_transport_drv.h"
#include <stdint.h>

/* SPI transfer context — not defined by core, defined here for testing.
 * The transport contract passes this as void* to g_h_transport.spi_transfer.
 * The actual struct layout is port-specific; this is the mock version. */
typedef struct {
    uint8_t *tx_buffer;
    uint16_t tx_length;
    uint8_t *rx_buffer;
    uint16_t rx_length;
} h_spi_transfer_t;

void test_transport_init_spi(void)
{
    void *handle = NULL;
    TEST_ASSERT_EQUAL(H_OK, h_transport_init(&handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(H_OK, h_transport_deinit(handle));
}

void test_transport_mock_transfer(void)
{
    void *handle = NULL;
    TEST_ASSERT_EQUAL(H_OK, h_transport_init(&handle));

    /* Simulate a TLV handshake transfer sequence:
     * CAPS_REQ → CAPS_RSP → SET_FTR → ACK */
    static uint8_t tx_data[] = {0x01, 0x00, 0x00, 0x00};
    static uint8_t rx_data[256] = {0};

    h_spi_transfer_t ctx = {
        .tx_buffer = tx_data,
        .tx_length = 4,
        .rx_buffer = rx_data,
        .rx_length = 0,
    };

    /* Transfer should succeed with mock (returns H_OK by default) */
    TEST_ASSERT_EQUAL(H_OK, h_spi_transfer(handle, &ctx));

    /* Verify transport deinit is idempotent */
    TEST_ASSERT_EQUAL(H_OK, h_transport_deinit(handle));
    TEST_ASSERT_EQUAL(H_OK, h_transport_deinit(handle)); /* second call — idempotent */
}

/* ── h_transport_drv.c: state machine & lifecycle ── */
void test_transport_drv_state_ready(void)
{
    /* Initial state after link: TRANSPORT_INACTIVE */
    TEST_ASSERT_EQUAL(0, is_transport_rx_ready());
    TEST_ASSERT_EQUAL(0, is_transport_tx_ready());

    /* RX_ACTIVE: RX ready, TX not ready */
    set_transport_state(TRANSPORT_RX_ACTIVE);
    TEST_ASSERT_EQUAL(1, is_transport_rx_ready());
    TEST_ASSERT_EQUAL(0, is_transport_tx_ready());

    /* TX_ACTIVE: both ready */
    set_transport_state(TRANSPORT_TX_ACTIVE);
    TEST_ASSERT_EQUAL(1, is_transport_rx_ready());
    TEST_ASSERT_EQUAL(1, is_transport_tx_ready());

    /* Reset to INACTIVE for next test */
    set_transport_state(TRANSPORT_INACTIVE);
}

void test_teardown_transport_safe(void)
{
    /* teardown without prior init must be safe (bus_handle is NULL) */
    h_err_t ret = teardown_transport();
    TEST_ASSERT_EQUAL(H_OK, ret);
}

void test_transport_drv_remove_channel_null(void)
{
    h_err_t ret = transport_drv_remove_channel(NULL);
    TEST_ASSERT_EQUAL(H_FAIL, ret);
}

void test_process_priv_communication_null(void)
{
    /* NULL input must not crash */
    process_priv_communication(NULL);
    /* No assertion — contract is "does not segfault" */
}
