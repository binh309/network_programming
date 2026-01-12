#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

#include <stddef.h>
#include "packet.h"
#include "network_errors.h"

#define MAX_PACKET_SIZE (MAX_BODY_LEN + 128)  // Limit to body size + header + margin

/**
 * @brief Checks if a buffer contains at least one complete packet.
 *
 * @param buffer The buffer containing received data.
 * @param bytes_in_buffer The total number of bytes in the buffer.
 * @return The total size of the first complete packet, or 0 if no complete
 *         packet is found.
 *
 * DEPRECATED: Use packet_parser_check_message_ex instead for error handling
 */
size_t packet_parser_has_complete_message(const char* buffer, size_t bytes_in_buffer);

/**
 * @brief Check if buffer has a complete packet with error codes
 *
 * This is the NEW error-aware version. Returns specific error codes instead of just 0/size.
 *
 * @param buffer The buffer containing received data
 * @param bytes_in_buffer Number of bytes in buffer
 * @param out_packet_size Pointer to receive packet size if complete (only valid if returns PARSE_OK)
 * @return PacketParseResult - one of PARSE_OK, PARSE_INCOMPLETE, PARSE_SIZE_EXCEEDED
 */
int packet_parser_check_message_ex(const char* buffer, size_t bytes_in_buffer, size_t* out_packet_size);

/**
 * @brief Deserializes a raw byte buffer into a structured packet.
 *
 * Assumes that the buffer contains a complete packet.
 *
 * @param buffer The raw byte buffer to parse.
 * @param packet The packet_t struct to populate.
 * @return 0 on success, -1 on error (e.g., NULL input).
 */
int packet_parser_deserialize(const char* buffer, packet_t* packet);

#endif // PACKET_PARSER_H
