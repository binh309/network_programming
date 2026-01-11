#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "see_my_stocks.h"
#include "../data/portfolio_db.h"
#include "../data/stock_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

// Handle see my stocks request
void handle_see_my_stocks_request(int client_socket, const packet_t* request, session_t* session) {
    printf("[MY_STOCKS] User %u requested to see their portfolio\n", session->user_id);

    portfolio_t* portfolio = portfolio_db_get(session->user_id);
    if (!portfolio || portfolio->holding_count == 0) {
        const char* msg = "Your portfolio is empty.";
        packet_t response;
        create_packet(&response, request->header.request_id, SMSG_VIEW_MY_STOCKS_DATA, msg);
        send_packet(client_socket, &response);
        if (portfolio) portfolio_db_free(portfolio);
        return;
    }

    // Allocate a large buffer for the response
    // Format: SYMBOL,QTY,AVG_PRICE,CURRENT_PRICE,PNL;...
    size_t buffer_size = portfolio->holding_count * 256;
    char* response_body = malloc(buffer_size);
    if (!response_body) {
        send_error(client_socket, request->header.request_id, "Server memory error.");
        portfolio_db_free(portfolio);
        return;
    }

    char* ptr = response_body;
    size_t remaining_size = buffer_size;
    double total_pnl = 0.0;

    for (uint32_t i = 0; i < portfolio->holding_count; i++) {
        holding_t* holding = &portfolio->holdings[i];
        stock_t* stock_data = stock_db_get_by_id(holding->stock_id);

        if (stock_data) {
            double current_value = holding->quantity * stock_data->last_price;
            double cost_basis = holding->quantity * holding->average_purchase_price;
            double pnl = current_value - cost_basis;
            total_pnl += pnl;

            int written = snprintf(ptr, remaining_size, "%s,%u,%.2f,%.2f,%.2f;",
                                   stock_data->symbol,
                                   holding->quantity,
                                   holding->average_purchase_price,
                                   stock_data->last_price,
                                   pnl);
            
            if (written < 0 || (size_t)written >= remaining_size) {
                stock_db_free(stock_data);
                break;
            }
            ptr += written;
            remaining_size -= written;
            stock_db_free(stock_data);
        }
    }
    
    // Append total P&L
    int written = snprintf(ptr, remaining_size, "\nTotal P&L: %.2f", total_pnl);
    if (written > 0 && (size_t)written < remaining_size) {
        ptr += written;
    }

    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_VIEW_MY_STOCKS_DATA, response_body);
    send_packet(client_socket, &response);

    free(response_body);
    portfolio_db_free(portfolio);

    printf("[MY_STOCKS] Sent portfolio details to user %u\n", session->user_id);
}
