#ifndef VIEW_STOCKS_H
#define VIEW_STOCKS_H

#include "../network/packet.h"
#include "../core/session_manager.h"

void handle_view_stocks_request(int client_socket, const packet_t* request, session_t* session);

#endif
