#ifndef VIEW_STOCKS_H
#define VIEW_STOCKS_H

#include "../network/packet.h"
#include "../core/connection_manager.h"

void handle_view_stocks_request(int client_socket, const packet_t* request, connection_t* connection);

#endif
