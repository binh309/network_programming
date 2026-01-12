#include "../ui/tui.h"
#include <stdio.h>
#include <string.h>
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
#include "../core/connection_manager.h"

// Check if a user is authenticated for a given request
static bool is_authenticated(connection_t* connection) {
    return connection && connection->is_logged_in;
}

/**
 * @brief Route incoming messages to appropriate handlers
 *
 * FIXES RESPONSE OWNERSHIP: This function now returns a status code
 * instead of sending responses directly. The request_handler owns the
 * response lifecycle.
 *
 * Strategy:
 * 1. Handlers still do their work (DB operations, validations)
 * 2. But instead of calling send_*() directly, they return status codes
 * 3. request_handler_process() uses these codes to send unified responses
 *
 * Migration Note: Existing handlers still call send_*() directly.
 * This refactoring should happen gradually:
 * - Phase 1: Dispatcher returns status, wraps send_*() calls (THIS COMMIT)
 * - Phase 2: Migrate handlers one-by-one to return status instead of sending
 * - Phase 3: Remove send_*() from handlers entirely
 */
dispatcher_status_t dispatcher_handle_message(int client_socket, const packet_t* packet, connection_t* connection) {
    server_debug("[DISPATCHER] Handling message type=0x%02x from fd=%d (response ownership in request_handler)\n", 
           packet->header.type, client_socket);

    // These messages can be handled without being logged in
    switch (packet->header.type) {
        case CMSG_REGISTER:
            server_debug("[DISPATCHER] → Routing to REGISTER handler\n");
            // Currently: handle_register_request() sends response directly
            // Migration needed: Make it return status code
            handle_register_request(client_socket, packet, connection);
            return DISP_OK;  // For now, assume success
            
        case CMSG_LOGIN:
            server_debug("[DISPATCHER] → Routing to LOGIN handler\n");
            // Currently: handle_login_request() sends response directly
            // Migration needed: Make it return status code
            handle_login_request(client_socket, packet, connection);
            server_debug("[DISPATCHER] ← LOGIN handler returned\n");
            return DISP_OK;  // For now, assume success (actual check in handler)
            
        case CMSG_LOGOUT:
        {
            server_debug("[DISPATCHER] → Routing to LOGOUT handler\n");
            // Handle logout: clear session and send response
            if (connection) {
                uint32_t user_id = connection->user_id;
                connection->is_logged_in = false;
                connection->user_id = 0;
                memset(connection->username, 0, sizeof(connection->username));
                
                // Clear user's portfolio from memory on logout
                if (user_id > 0) {
                    extern void portfolio_mgr_clear_user(uint32_t user_id);
                    portfolio_mgr_clear_user(user_id);
                }
            }
            
            packet_t response;
            create_packet(&response, packet->header.request_id, SMSG_LOGOUT_SUCCESS, "Logged out successfully");
            send_packet(client_socket, &response);
            server_debug("[DISPATCHER] ← LOGOUT handler returned\n");
            return DISP_OK;
        }
    }

    // All messages below require the user to be logged in
    if (!is_authenticated(connection)) {
        // RESPONSE OWNERSHIP: For now, still send here, but this should move to request_handler
        send_error(client_socket, packet->header.request_id, "Not authenticated. Please log in first.");
        return DISP_INVALID_ARGS;
    }

    switch (packet->header.type) {
        case CMSG_VIEW_STOCKS:
            // TODO: Migrate handle_view_stocks_request to return status code
            handle_view_stocks_request(client_socket, packet, connection);
            return DISP_OK;
            
        case CMSG_BUY_STOCK:
            // TODO: Migrate handle_buy_stock_request to return status code
            handle_buy_stock_request(client_socket, packet, connection);
            return DISP_OK;
            
        case CMSG_SELL_STOCK:
            // TODO: Migrate handle_sell_stock_request to return status code
            handle_sell_stock_request(client_socket, packet, connection);
            return DISP_OK;
            
        case CMSG_SEE_BALANCE:
            // TODO: Migrate handle_see_balance_request to return status code
            handle_see_balance_request(client_socket, packet, connection);
            return DISP_OK;
            
        case CMSG_VIEW_MY_STOCKS:
            // TODO: Migrate handle_see_my_stocks_request to return status code
            handle_see_my_stocks_request(client_socket, packet, connection);
            return DISP_OK;
            
        default:
            server_debug("[DISPATCHER] Unknown message type for authenticated user: 0x%02x\n", packet->header.type);
            send_error(client_socket, packet->header.request_id, "Unknown or invalid request.");
            return DISP_UNKNOWN_COMMAND;
    }
}