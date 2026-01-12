#ifndef REGISTER_H
#define REGISTER_H

#include "../network/packet.h"
#include "../core/connection_manager.h"

void handle_register_request(int client_socket, const packet_t* request, connection_t* connection);

#endif
