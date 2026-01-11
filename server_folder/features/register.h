#ifndef REGISTER_H
#define REGISTER_H

#include "../network/packet.h"
#include "../core/session_manager.h"

void handle_register_request(int client_socket, const packet_t* request, session_t* session);

#endif
