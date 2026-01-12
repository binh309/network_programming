#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include "connection_manager.h"

/**
 * @brief Process one event on a client socket
 *
 * Orchestrates the full request lifecycle:
 * 1. Receive bytes from socket
 * 2. Buffer incomplete packets
 * 3. Parse complete packets
 * 4. Route to dispatcher
 * 5. Send response back
 *
 * Called by event loop when socket is readable
 *
 * @param client_fd The socket file descriptor with data available
 * @return 0 on success, -1 if connection should close
 */
int request_handler_process(int client_fd);

#endif // REQUEST_HANDLER_H
