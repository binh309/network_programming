#ifndef SEE_BALANCE_H
#define SEE_BALANCE_H

#include "../network/packet.h"
#include "../core/connection_manager.h"

void handle_see_balance_request(int client_socket, const packet_t* request, connection_t* connection);

#endif
