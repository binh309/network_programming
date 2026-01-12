#ifndef SELL_STOCK_H
#define SELL_STOCK_H

#include "../network/packet.h"
#include "../core/connection_manager.h"

void handle_sell_stock_request(int client_socket, const packet_t* request, connection_t* connection);

#endif
