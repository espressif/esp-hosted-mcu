/* host/port/include/h_test_only.h
 * Internal functions exposed only for unit testing.
 * This file is NOT used by production code. */
#ifndef H_TEST_ONLY_H
#define H_TEST_ONLY_H

#ifdef H_BUILD_TESTS
#include "h_types.h"

/* Expose contract validation for vtable NULL-protection tests */
h_err_t h_validate_contracts(void);

/* Expose transport frame processing for protocol-level tests */
h_err_t h_transport_drv_process_frame(uint8_t *frame, uint16_t len);

#endif /* H_BUILD_TESTS */
#endif /* H_TEST_ONLY_H */
