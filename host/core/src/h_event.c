/* host/core/src/h_event.c
 *
 * Event dispatcher — thin wrapper routing h_event_post_internal()
 * to the port-level event bus via g_h_event.post(). */

#include "h_event_post.h"
#include "h_wrapper.h"

h_err_t h_event_post_internal(h_event_base_t base, int32_t event_id,
                               void *event_data, size_t event_data_size)
{
    return h_event_post(base, event_id, event_data, event_data_size);
}
