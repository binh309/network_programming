#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "see_balance.h"
#include "../data/account_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

// Handle see balance request
void handle_see_balance_request(int client_socket, const packet_t* request, connection_t* connection) {
    if (!connection->is_logged_in) {
        send_error(client_socket, request->header.request_id, "You must be logged in to see your balance.");
        return;
    }

    double balance = account_db_get_balance(connection->user_id);
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Current Balance: $%.2f", balance);

    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_SEE_BALANCE_DATA, msg);
    send_packet(client_socket, &response);
}