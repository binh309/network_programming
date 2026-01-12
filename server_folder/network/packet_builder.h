#ifndef PACKET_BUILDER_H
#define PACKET_BUILDER_H

#include "packet.h"
#include <stddef.h>

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
int packet_builder_serialize(const packet_t* packet, char* buffer, size_t buffer_size, size_t* serialized_size);

#endif // PACKET_BUILDER_H
