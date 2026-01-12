#ifndef SEE_MY_STOCKS_H
#define SEE_MY_STOCKS_H

#include "../network/packet.h"
#include "../core/connection_manager.h"

void handle_see_my_stocks_request(int client_socket, const packet_t* request, connection_t* connection);

#endif
