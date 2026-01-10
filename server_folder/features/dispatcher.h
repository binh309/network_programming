#ifndef DISPATCHER_H
#define DISPATCHER_H

#include <stdint.h>
#include "../network/protocol.h"

// Function declarations
void dispatcher_handle_message(int client_fd, struct packet_header* header, char* payload);

#endif
