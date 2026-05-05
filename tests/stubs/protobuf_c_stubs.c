/* Minimal stubs for protobuf-c runtime functions.
 * Required by common/proto/esp_hosted_rpc.pb-c.c when linking
 * h_rpc_req.c / h_rpc_rsp.c / h_rpc_evt.c in mock builds.
 */
#include "esp_hosted_rpc.pb-c.h"
#include <stdlib.h>
#include <string.h>

size_t protobuf_c_message_get_packed_size(const ProtobufCMessage *message)
{
    (void)message;
    return 0;
}

size_t protobuf_c_message_pack(const ProtobufCMessage *message, uint8_t *out)
{
    (void)message;
    (void)out;
    return 0;
}

size_t protobuf_c_message_pack_to_buffer(const ProtobufCMessage *message,
                                          ProtobufCBuffer *buffer)
{
    (void)message;
    (void)buffer;
    return 0;
}

ProtobufCMessage *protobuf_c_message_unpack(const ProtobufCMessageDescriptor *descriptor,
                                             ProtobufCAllocator  *allocator,
                                             size_t               len,
                                             const uint8_t       *data)
{
    (void)descriptor;
    (void)allocator;
    (void)len;
    (void)data;
    return NULL;
}

void protobuf_c_message_free_unpacked(ProtobufCMessage *message,
                                       ProtobufCAllocator *allocator)
{
    (void)message;
    (void)allocator;
}

/* Stub for h_rpc_core.c:rpc_id_name() */
const ProtobufCEnumValue *protobuf_c_enum_descriptor_get_value(
    const ProtobufCEnumDescriptor *descriptor, int value)
{
    (void)descriptor;
    (void)value;
    return NULL;
}
