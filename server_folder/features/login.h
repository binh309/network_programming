#ifndef LOGIN_H
#define LOGIN_H

#include "../network/packet.h"
#include "../core/session_manager.h"

void handle_login_request(int client_socket, const packet_t* request, session_t* session);

#endif
