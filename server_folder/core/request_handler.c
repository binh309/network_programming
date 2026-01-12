#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "request_handler.h"
#include "connection_manager.h"
#include "../network/network_receive.h"
#include "../network/network_send.h"
#include "../network/packet_parser.h"
#include "../network/packet_builder.h"
#include "../network/network_errors.h"
#include "../features/dispatcher.h"
#include "../model/error.h"

/**
 * @brief Process one incoming request on a client socket
 *
 * THREAD SAFETY: This function is called by the event loop for socket events.
 * With epoll, the SAME socket could theoretically have multiple events queued.
 * 
 * Pattern:
 * 1. Call network_receive OUTSIDE lock (blocking I/O)
 * 2. Acquire per-connection lock
 * 3. Append data and process packets INSIDE lock
 * 4. Release lock before returning
 *
 * This prevents data corruption from concurrent access to read_buffer.
 */
int request_handler_process(int client_fd) {
    // Step 1: Get connection
    connection_t* conn = connection_mgr_get(client_fd);
    if (!conn) {
        fprintf(stderr, "[HANDLER] Connection not found for fd %d\n", client_fd);
        return -1;  // Close connection
    }

    // Step 2: Validate connection state (without lock - just a peek)
    ConnectionState state = connection_mgr_get_state(conn);
    if (state == CONN_CLOSED || state == CONN_CLOSING) {
        printf("[HANDLER] Connection fd %d is closing/closed (state=%d), ignoring\n", client_fd, state);
        return -1;  // Close connection
    }

    // Step 3: Receive bytes from socket (Layer 1 - stateless)
    // THIS MUST HAPPEN OUTSIDE THE LOCK to avoid blocking other threads
    char recv_buffer[4096];
    ssize_t bytes_read = network_receive(client_fd, recv_buffer, sizeof(recv_buffer));

    if (bytes_read < 0) {
        // Check for expected non-blocking errors
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available right now, just return
            return 0;
        }
        // Real error occurred
        perror("[HANDLER] network_receive");
        connection_mgr_set_state(conn, CONN_CLOSING);
        return -1;  // Close connection
    }

    if (bytes_read == 0) {
        // Client closed connection gracefully
        printf("[HANDLER] Client on fd %d closed connection (EOF)\n", client_fd);
        connection_mgr_set_state(conn, CONN_CLOSING);
        return -1;  // Close connection
    }

    printf("[HANDLER] Received %ld bytes on fd %d\n", bytes_read, client_fd);
    printf("[DEBUG] buffer_offset before append=%d\n", conn->read_offset);
    fflush(stdout);

    // CRITICAL SECTION: Acquire per-connection lock
    // This protects the read_buffer from concurrent access
    pthread_mutex_lock(&conn->state_lock);
    printf("[DEBUG] Lock acquired\n");
    fflush(stdout);

    // Step 4: Append received bytes to connection buffer (Layer 3)
    if (connection_mgr_append_data(conn, recv_buffer, bytes_read) < 0) {
        // Buffer overflow - client sending too much data without completing packet
        fprintf(stderr, "[HANDLER] Buffer overflow on fd %d - rejecting client\n", client_fd);
        pthread_mutex_unlock(&conn->state_lock);
        connection_mgr_set_state(conn, CONN_CLOSING);
        return -1;  // Close connection
    }
    
    printf("[DEBUG] After append_data: read_offset=%d\n", conn->read_offset);
    printf("[DEBUG] Buffer contents (first 50 bytes): %.50s\n", conn->read_buffer);
    fflush(stdout);

    // Step 5: Process all complete packets in buffer
    // (TCP might deliver multiple packets in one read)
    printf("[DEBUG] Entering while loop\n");
    fflush(stdout);
    while (1) {
        printf("[DEBUG] While loop iteration start\n");
        fflush(stdout);
        // Check if we have a complete packet using new error-aware function
        size_t packet_size = 0;
        int parse_result = packet_parser_check_message_ex(
            conn->read_buffer,
            conn->read_offset,
            &packet_size
        );

        printf("[DEBUG] packet_parser_check_message_ex result=%d, packet_size=%lu, buffer_offset=%d\n",
               parse_result, packet_size, conn->read_offset);

        if (parse_result == PARSE_INCOMPLETE) {
            printf("[DEBUG] Packet incomplete, waiting for more bytes\n");
            // No more complete packets, wait for next epoll event
            break;
        }

        if (parse_result == PARSE_SIZE_EXCEEDED) {
            // Client sent oversized packet - DoS attempt
            fprintf(stderr, "[HANDLER] Oversized packet detected on fd %d - closing connection\n", client_fd);
            pthread_mutex_unlock(&conn->state_lock);
            connection_mgr_set_state(conn, CONN_CLOSING);
            return -1;  // Close connection
        }

        if (parse_result != PARSE_OK) {
            // Other parse error
            fprintf(stderr, "[HANDLER] Packet parse error on fd %d (code %d)\n", client_fd, parse_result);
            pthread_mutex_unlock(&conn->state_lock);
            send_error(client_fd, 0, "Invalid packet format");
            connection_mgr_set_state(conn, CONN_CLOSING);
            return -1;
        }

        printf("[HANDLER] Found complete packet size=%zu\n", packet_size);

        // Step 6: Parse the packet (Layer 2 - stateless)
        packet_t packet;
        if (packet_parser_deserialize(conn->read_buffer, &packet) < 0) {
            fprintf(stderr, "[HANDLER] Failed to deserialize packet on fd %d\n", client_fd);
            pthread_mutex_unlock(&conn->state_lock);
            send_error(client_fd, 0, "Invalid packet format");
            connection_mgr_set_state(conn, CONN_CLOSING);
            return -1;  // Close connection
        }

        printf("[HANDLER] Parsed packet type=0x%02x, request_id=%u, body_length=%u\n",
               packet.header.type, packet.header.request_id, packet.header.length);
        printf("[DEBUG] Packet body (first 50 chars): %.50s\n", 
               (char*)packet.body);

        // Step 7: Transition to PROCESSING state
        ConnectionState before_state = conn->state;
        conn->state = CONN_PROCESSING;
        printf("[CONN_MGR] Socket %d: state %d -> %d\n", conn->client_socket, before_state, CONN_PROCESSING);
        printf("[DEBUG] State transition: %d → %d (PROCESSING)\n", before_state, CONN_PROCESSING);

        // Step 8: UNLOCK before calling dispatcher
        // Dispatcher may do long operations (database queries, etc.)
        // We don't want to hold the connection lock during business logic
        pthread_mutex_unlock(&conn->state_lock);
        printf("[DEBUG] Unlocked, calling dispatcher\n");

        // Route the message to appropriate handler (business logic)
        // NOTE: Dispatcher sends response directly (maintains existing behavior)
        dispatcher_handle_message(client_fd, &packet, conn);
        printf("[DEBUG] Dispatcher returned\n");

        // Step 9: RELOCK after dispatcher returns
        pthread_mutex_lock(&conn->state_lock);
        printf("[DEBUG] Relocked after dispatcher\n");

        // Step 10: Transition back to READY state
        conn->state = CONN_READY;
        printf("[CONN_MGR] Socket %d: state %d -> %d\n", conn->client_socket, CONN_PROCESSING, CONN_READY);
        printf("[DEBUG] State transition back to READY\n");

        // Step 11: Consume the processed packet from buffer (Layer 3)
        connection_mgr_consume_packet(conn, packet_size);
        printf("[HANDLER] Consumed %zu byte packet, buffer now has %d bytes remaining\n",
               packet_size, connection_mgr_get_buffer_offset_unsafe(conn));
    }

    // END CRITICAL SECTION: Release lock
    pthread_mutex_unlock(&conn->state_lock);

    return 0;  // Connection still open
}
