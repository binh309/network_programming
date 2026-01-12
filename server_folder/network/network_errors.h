#ifndef NETWORK_ERRORS_H
#define NETWORK_ERRORS_H

/**
 * @brief Error codes for network_receive function
 *
 * Defines clear error contract between network layer and orchestration layer
 */
typedef enum {
    RECV_OK         =  0,  // Successfully read data (bytes_read > 0)
    RECV_NO_DATA    =  1,  // No data available (EAGAIN/EWOULDBLOCK) - NOT an error, just wait
    RECV_EOF        = -1,  // Client gracefully closed connection (bytes_read == 0)
    RECV_ERROR      = -2   // System error (check errno for details)
} NetworkRecvResult;

/**
 * @brief Error codes for packet parser functions
 *
 * Defines clear error contract between protocol layer and orchestration layer
 */
typedef enum {
    PARSE_OK                =  0,  // Packet successfully parsed
    PARSE_INCOMPLETE        =  1,  // Not enough data yet - wait for more
    PARSE_HEADER_INVALID    = -1,  // Header corrupted or invalid
    PARSE_BODY_INVALID      = -2,  // Body doesn't match declared size
    PARSE_SIZE_EXCEEDED     = -3,  // Packet declares size > MAX_PACKET_SIZE (DoS attempt)
    PARSE_MESSAGE_UNKNOWN   = -4   // Message type not recognized
} PacketParseResult;

#endif // NETWORK_ERRORS_H
