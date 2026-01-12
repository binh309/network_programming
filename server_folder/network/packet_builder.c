#include "packet_builder.h"
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>

/**
 * @brief Serializes a packet structure into a raw byte buffer for network transmission.
 *
 * @param packet The packet to serialize.
 * @param buffer The output buffer to write the serialized data into.
 * @param buffer_size The total size of the output buffer.
 * @param serialized_size A pointer to a size_t variable that will be populated with
 *                        the total size of the serialized packet.
 * @return 0 on success, -1 on error (e.g., buffer too small).
 */
int packet_builder_serialize(const packet_t* packet, char* buffer, size_t buffer_size, size_t* serialized_size) {
    if (packet == NULL || buffer == NULL || serialized_size == NULL) {
        return -1;
    }

    size_t required_size = sizeof(packet_header_t) + packet->header.length;
    if (buffer_size < required_size) {
        fprintf(stderr, "[BUILDER] Error: Buffer too small for serialization. Need %zu, have %zu.\n", required_size, buffer_size);
        return -1;
    }

    // Prepare header for network transmission (network byte order)
    packet_header_t* net_header = (packet_header_t*)buffer;
    net_header->request_id = htons(packet->header.request_id);
    net_header->type = packet->header.type;
    net_header->length = htons(packet->header.length);

    // Copy body
    if (packet->header.length > 0) {
        memcpy(buffer + sizeof(packet_header_t), packet->body, packet->header.length);
    }

    *serialized_size = required_size;
    return 0;
}
