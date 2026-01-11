#include <stdio.h>
#include "dispatcher.h"
#include "../network/protocol.h"
#include "register.h"
#include "login.h"
#include "view_stocks.h"
#include "buy_stock.h"
#include "sell_stock.h"
#include "see_balance.h"
#include "see_my_stocks.h"
#include "../model/error.h"

// Check if a user is authenticated for a given request
static bool is_authenticated(session_t* session) {
    return session && session->is_logged_in;
}

// Route incoming messages to appropriate handlers
void dispatcher_handle_message(int client_socket, const packet_t* packet, session_t* session) {
    printf("[DISPATCHER] Handling message type=0x%02x from fd=%d\n", packet->header.type, client_socket);

    // These messages can be handled without being logged in
    switch (packet->header.type) {
        case CMSG_REGISTER:
            handle_register_request(client_socket, packet, session);
            return;
        case CMSG_LOGIN:
            handle_login_request(client_socket, packet, session);
            return;
        case CMSG_LOGOUT:
            // handle_logout_request(client_socket, packet, session);
            return;
    }

    // All messages below require the user to be logged in
    if (!is_authenticated(session)) {
        send_error(client_socket, packet->header.request_id, "Not authenticated. Please log in first.");
        return;
    }

    switch (packet->header.type) {
        case CMSG_VIEW_STOCKS:
            handle_view_stocks_request(client_socket, packet, session);
            break;
        case CMSG_BUY_STOCK:
            handle_buy_stock_request(client_socket, packet, session);
            break;
        case CMSG_SELL_STOCK:
            handle_sell_stock_request(client_socket, packet, session);
            break;
        case CMSG_SEE_BALANCE:
            handle_see_balance_request(client_socket, packet, session);
            break;
        case CMSG_VIEW_MY_STOCKS:
            handle_see_my_stocks_request(client_socket, packet, session);
            break;
        default:
            printf("[DISPATCHER] Unknown message type for authenticated user: 0x%02x\n", packet->header.type);
            send_error(client_socket, packet->header.request_id, "Unknown or invalid request.");
            break;
    }
}