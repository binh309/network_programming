#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "see_balance.h"
#include "../data/account_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"

// Handle see balance request
void handle_see_balance_request(int client_socket, const packet_t* request, session_t* session) {
    printf("[BALANCE] User %u requested to see their balance\n", session->user_id);

    double balance = account_db_get_balance(session->user_id);

    char response_body[64];
    snprintf(response_body, sizeof(response_body), "Current Balance: %.2f", balance);

    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_SEE_BALANCE_DATA, response_body);
    send_packet(client_socket, &response);

    printf("[BALANCE] Sent balance of %.2f to user %u\n", balance, session->user_id);
}