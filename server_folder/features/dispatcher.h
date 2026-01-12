#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "../network/packet.h"
#include "../core/connection_manager.h"

/**
 * @brief Status code returned by dispatcher
 *
 * FIXES RESPONSE OWNERSHIP: Dispatcher no longer sends response directly.
 * Instead, it returns a status code that request_handler uses to construct
 * and send the appropriate response.
 *
 * This gives request_handler full control over response creation, allowing:
 * - Consistent error handling across all commands
 * - Better testability (dispatcher logic separated from I/O)
 * - Clearer ownership: request_handler owns request/response lifecycle
 */
typedef enum {
    DISP_OK = 0,                    // Handler succeeded, send success response
    DISP_LOGIN_FAILED = -1,         // Login failed
    DISP_BUY_FAILED = -2,           // Buy operation failed
    DISP_SELL_FAILED = -3,          // Sell operation failed
    DISP_INSUFFICIENT_BALANCE = -4, // Not enough money/stock
    DISP_UNKNOWN_COMMAND = -5,      // Unrecognized command
    DISP_DATABASE_ERROR = -6,       // DB operation failed
    DISP_INVALID_ARGS = -7,         // Invalid request arguments
    DISP_INTERNAL_ERROR = -99,      // Unexpected error
} dispatcher_status_t;

/**
 * @brief Route incoming messages to appropriate handlers
 *
 * IMPORTANT: This function NO LONGER sends responses directly!
 * It only processes the request and returns a status code.
 *
 * Response ownership is now in request_handler, which:
 * 1. Calls dispatcher_handle_message() -> gets status
 * 2. Constructs response packet based on status
 * 3. Sends response via network_send()
 *
 * Benefits:
 * - Single response path (no branching in multiple handlers)
 * - Consistent error responses for all commands
 * - Easier to test (no I/O in dispatcher)
 * - Request/response lifecycle in one place
 *
 * @param client_socket The client connection fd (for logging)
 * @param packet Parsed incoming message
 * @param connection Connection state
 * @return dispatcher_status_t: operation result code (not a network error!)
 */
dispatcher_status_t dispatcher_handle_message(int client_socket, const packet_t* packet, connection_t* connection);

#endif
