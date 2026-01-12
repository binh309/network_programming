#include "ui/tui.h"
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "../network/protocol.h"

// Sends a standardized error message to the client
void send_error(int client_socket, uint16_t request_id, const char* message) {
    server_debug("[ERROR] Sending to socket %d (req_id: %u): %s\n", client_socket, request_id, message);
    
    packet_t response;
    create_packet(&response, request_id, SMSG_ERROR, message);
    send_packet(client_socket, &response);
}

