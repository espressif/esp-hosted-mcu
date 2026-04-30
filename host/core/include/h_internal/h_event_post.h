/* host/core/include/h_internal/h_event_post.h */
#ifndef H_EVENT_POST_H
#define H_EVENT_POST_H

#include "h_types.h"

/* Internal (core-layer only) event posting.
 * Applications use h_event_post via h_wrapper.h.
 * Core code calls this to route events from RPC layer to the port event bus. */
h_err_t h_event_post_internal(h_event_base_t base, int32_t event_id,
                              void *event_data, size_t event_data_size);

#endif
