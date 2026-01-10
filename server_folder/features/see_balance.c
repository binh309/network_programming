#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "../network/protocol.h"
#include "../core/session_manager.h"
#include "../data/account_db.h"
#include "see_balance.h"

void handle_see_balance_request(int client_fd) {
    printf("[SEE_BALANCE] Request from fd=%d\n", client_fd);

    struct see_balance_response resp;
    memset(&resp, 0, sizeof(resp));

    // Step 1: Check authentication
    uint32_t user_id = 0;
    printf("[SEE_BALANCE] Checking authentication for client fd=%d\n", client_fd);
    if (!session_mgr_is_authenticated(client_fd, &user_id)) {
        resp.status = STATUS_NOT_AUTHENTICATED;
        resp.balance = 0.0;
        snprintf(resp.message, 128, "Not authenticated. Login first");
        printf("[SEE_BALANCE] Error: Not authenticated\n");
        goto send_response;
    }

    printf("[SEE_BALANCE] Client authenticated as user %u\n", user_id);

    // Step 2: Get user balance
    double balance = account_db_get_balance(user_id);
    printf("[SEE_BALANCE] Retrieved balance: $%.2f\n", balance);
    
    resp.status = STATUS_SUCCESS;
    resp.balance = balance;
    snprintf(resp.message, 128, "Balance: $%.2f", balance);

    printf("[SEE_BALANCE] User %u balance: $%.2f\n", user_id, balance);

send_response:
    // Send response
    struct packet_header resp_hdr = {
        .type = MSG_SEE_BALANCE_RESPONSE,
        .length = sizeof(resp)
    };

    resp.message_length = strlen(resp.message);
    
    printf("[SEE_BALANCE] Sending response: status=%u, balance=%.2f\n", resp.status, resp.balance);
    
    // Send header + payload together
    char buffer[sizeof(struct packet_header) + sizeof(resp)];
    memcpy(buffer, &resp_hdr, sizeof(resp_hdr));
    memcpy(buffer + sizeof(resp_hdr), &resp, sizeof(resp));
    send(client_fd, buffer, sizeof(buffer), 0);
    
    printf("[SEE_BALANCE] Response sent\n");
}
