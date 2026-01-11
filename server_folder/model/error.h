#ifndef ERROR_H
#define ERROR_H

#include "../network/packet.h"

// Sends a standardized error message to the client
void send_error(int client_socket, uint16_t request_id, const char* message);

#endif
