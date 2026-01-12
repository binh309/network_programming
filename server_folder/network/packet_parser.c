#include "packet_parser.h"
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>

/**
 * @brief Checks if a buffer contains at least one complete packet.
 *
 * @param buffer The buffer containing received data.
 * @param bytes_in_buffer The total number of bytes in the buffer.
 * @return The total size of the first complete packet, or 0 if no complete
 *         packet is found.
 *
 * OLD VERSION - kept for compatibility but should use packet_parser_check_message_ex
 */
size_t packet_parser_has_complete_message(const char* buffer, size_t bytes_in_buffer) {
    if (bytes_in_buffer < sizeof(packet_header_t)) {
        return 0; // Not even a full header yet
    }

    const packet_header_t* net_header = (const packet_header_t*)buffer;
    uint16_t body_len = ntohs(net_header->length);
    
    // SECURITY: Reject oversized packets
    if (body_len > MAX_PACKET_SIZE) {
        fprintf(stderr, "[PARSER] SECURITY: Packet body size %u exceeds limit %u\n", 
                body_len, MAX_PACKET_SIZE);
        return 0;  // Treat as incomplete to trigger connection close
    }
    
    size_t total_packet_size = sizeof(packet_header_t) + body_len;

    if (bytes_in_buffer >= total_packet_size) {
        return total_packet_size;
    }

    return 0; // Not enough data for the full packet body
}

/**
 * @brief Check if buffer has a complete packet with error codes (NEW)
 *
 * This function returns specific error codes instead of just 0/size,
 * allowing proper error handling in orchestration layer.
 *
 * SECURITY: Validates packet size against MAX_PACKET_SIZE
 *
 * @return PARSE_OK if complete packet found (out_packet_size set)
 *         PARSE_INCOMPLETE if more data needed
 *         PARSE_SIZE_EXCEEDED if packet declares size > MAX_PACKET_SIZE (DoS attempt)
 */
int packet_parser_check_message_ex(const char* buffer, size_t bytes_in_buffer, size_t* out_packet_size) {
    if (!buffer || !out_packet_size) {
        return PARSE_HEADER_INVALID;
    }

    // Check if we have at least the header
    if (bytes_in_buffer < sizeof(packet_header_t)) {
        return PARSE_INCOMPLETE;  // Need more data
    }

    // Read declared body length from header
    const packet_header_t* net_header = (const packet_header_t*)buffer;
    uint16_t declared_body_len = ntohs(net_header->length);
    
    // SECURITY CHECK: Reject oversized packets
    if (declared_body_len > MAX_PACKET_SIZE) {
        fprintf(stderr, "[PARSER] SECURITY VIOLATION: Packet body size %u exceeds max %u "
                "(possible DoS attempt)\n", declared_body_len, MAX_PACKET_SIZE);
        return PARSE_SIZE_EXCEEDED;  // Reject immediately
    }
    
    // Calculate total packet size
    size_t total_packet_size = sizeof(packet_header_t) + declared_body_len;
    
    // Check if full packet is in buffer
    if (bytes_in_buffer >= total_packet_size) {
        *out_packet_size = total_packet_size;
        return PARSE_OK;  // Complete packet ready
    }
    
    return PARSE_INCOMPLETE;  // Need more data
}

/**
 * @brief Deserializes a raw byte buffer into a structured packet.
 *
 * Assumes that the buffer contains a complete packet.
 *
 * @param buffer The raw byte buffer to parse.
 * @param packet The packet_t struct to populate.
 * @return 0 on success, -1 on error (e.g., NULL input).
 */
int packet_parser_deserialize(const char* buffer, packet_t* packet) {
    if (buffer == NULL || packet == NULL) {
        return -1;
    }

    const packet_header_t* net_header = (const packet_header_t*)buffer;
    uint16_t body_len = ntohs(net_header->length);

    packet->header.request_id = ntohs(net_header->request_id);
    packet->header.type = net_header->type;
    packet->header.length = body_len;

    if (body_len > 0) {
        if (body_len >= MAX_BODY_LEN) {
             fprintf(stderr, "[PARSER] Warning: Received body length (%u) is too large, truncating.\n", body_len);
             body_len = MAX_BODY_LEN - 1;
             packet->header.length = body_len;
        }
        memcpy(packet->body, buffer + sizeof(packet_header_t), body_len);
    }
    packet->body[body_len] = '\0';

    return 0;
}
