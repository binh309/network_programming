#ifndef BUY_STOCK_H
#define BUY_STOCK_H

#include "../network/packet.h"
#include "../core/session_manager.h"

void handle_buy_stock_request(int client_socket, const packet_t* request, session_t* session);

#endif
