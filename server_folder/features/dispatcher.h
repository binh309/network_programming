#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "../network/packet.h"
#include "../core/session_manager.h"

// Function to route incoming messages
void dispatcher_handle_message(int client_socket, const packet_t* packet, session_t* session);

#endif
