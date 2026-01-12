#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "see_my_stocks.h"
#include "../data/portfolio_db.h"
#include "../data/stock_db.h"
#include "../core/portfolio_manager.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

// Handle see my stocks request
void handle_see_my_stocks_request(int client_socket, const packet_t* request, connection_t* connection) {
    if (!connection->is_logged_in) {
        send_error(client_socket, request->header.request_id, "You must be logged in to see your stocks.");
        return;
    }

    // Use portfolio_mgr_get_or_create for consistency with buy/sell operations
    portfolio_t* portfolio = portfolio_mgr_get_or_create(connection->user_id);
    if (!portfolio || portfolio->holding_count == 0) {
        char* msg = "You do not own any stocks.";
        packet_t response;
        create_packet(&response, request->header.request_id, SMSG_VIEW_MY_STOCKS_DATA, msg);
        send_packet(client_socket, &response);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }

    // Allocate buffer for response
    // Format: SYMBOL,QTY,AVG_PRICE,CURRENT_PRICE,PNL;...
    size_t buffer_size = 4096; 
    char* response_body = malloc(buffer_size);
    if (!response_body) {
        send_error(client_socket, request->header.request_id, "Server memory error.");
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }
    response_body[0] = '\0';

    size_t current_len = 0;
    
    for (uint32_t i = 0; i < portfolio->holding_count; i++) {
        holding_t* holding = &portfolio->holdings[i];
        stock_t* stock = stock_db_get_by_id(holding->stock_id);
        
        if (stock) {
            double current_price = stock->last_price;
            double total_cost = holding->quantity * holding->average_purchase_price;
            double current_value = holding->quantity * current_price;
            double pnl = current_value - total_cost;

            char line[256];
            snprintf(line, sizeof(line), "%s,%u,%.2f,%.2f,%.2f;", 
                     stock->symbol, 
                     holding->quantity, 
                     holding->average_purchase_price, 
                     current_price, 
                     pnl);
            
            size_t line_len = strlen(line);
            if (current_len + line_len < buffer_size - 1) {
                strcat(response_body, line);
                current_len += line_len;
            }
            
            stock_db_free(stock);
        }
    }

    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_VIEW_MY_STOCKS_DATA, response_body);
    send_packet(client_socket, &response);

    free(response_body);
    // NOTE: Don't free portfolio - it's managed by portfolio_manager
}
