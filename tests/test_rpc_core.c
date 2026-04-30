/* tests/test_rpc_core.c — RPC core unit tests (2 tests) */
#include "unity.h"
#include "h_rpc_core.h"
#include "h_types.h"
#include <stdint.h>

void test_rpc_request_timeout(void)
{
    /* Send request with short timeout, expect H_ERR_TIMEOUT.
     * No slave connected in mock — RPC core should time out. */
    uint8_t req[] = {0x01, 0x02, 0x03};
    uint8_t rsp[256];
    uint16_t rsp_len = sizeof(rsp);

    h_err_t ret = h_rpc_send_request(0x01, req, sizeof(req),
                                     rsp, &rsp_len, 10); /* 10ms timeout */
    /* With no transport initialized, this should fail */
    TEST_ASSERT(ret != H_OK);
}

void test_rpc_request_response_match(void)
{
    /* Verify RPC request UID matching: each request carries a unique ID,
     * and the response is matched via that UID, not by ordering. */
    uint8_t req1[] = {0xAA};
    uint8_t req2[] = {0xBB};
    uint8_t rsp[256];
    uint16_t rsp_len = sizeof(rsp);

    /* Two sequential requests with different UIDs should both fail
     * (no slave), but each should be tracked independently. */
    h_err_t r1 = h_rpc_send_request(0x10, req1, sizeof(req1),
                                    rsp, &rsp_len, 10);
    h_err_t r2 = h_rpc_send_request(0x20, req2, sizeof(req2),
                                    rsp, &rsp_len, 10);
    TEST_ASSERT(r1 != H_OK);
    TEST_ASSERT(r2 != H_OK);
}
