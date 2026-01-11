#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sell_stock.h"
#include "../data/stock_db.h"
#include "../data/account_db.h"
#include "../data/portfolio_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

// Handle sell stock request
void handle_sell_stock_request(int client_socket, const packet_t* request, session_t* session) {
    // 1. Parse request: "STOCK_ID,QUANTITY,PRICE,TYPE"
    uint16_t stock_id;
    uint32_t quantity;
    double price;
    char type[10] = {0};

    if (sscanf(request->body, "%hu,%u,%lf,%9s", &stock_id, &quantity, &price, type) != 4) {
        send_error(client_socket, request->header.request_id, "Invalid sell request format. Use: STOCK_ID,QTY,PRICE,TYPE");
        return;
    }

    printf("[SELL] User %u wants to sell %u of stock %hu at %.2f (%s)\n", session->user_id, quantity, stock_id, price, type);
    
    // 2. Get stock and user data
    stock_t* stock = stock_db_get_by_id(stock_id);
    if (!stock) {
        send_error(client_socket, request->header.request_id, "Stock not found.");
        return;
    }

    portfolio_t* portfolio = portfolio_db_get(session->user_id);
    if (!portfolio) {
        send_error(client_socket, request->header.request_id, "Could not retrieve portfolio.");
        stock_db_free(stock);
        return;
    }

    // 3. Validate order
    uint32_t current_holding = 0;
    for (uint32_t i = 0; i < portfolio->holding_count; ++i) {
        if (portfolio->holdings[i].stock_id == stock_id) {
            current_holding = portfolio->holdings[i].quantity;
            break;
        }
    }

    if (current_holding < quantity) {
        send_error(client_socket, request->header.request_id, "Insufficient holdings to sell.");
        goto cleanup;
    }

    // 4. Process order
    bool is_market_order = (strcmp(type, "MARKET") == 0);
    double exec_price = is_market_order ? stock->last_price : price;
    
    // For LIMIT orders, check if the price is valid for an immediate fill
    if (!is_market_order && price > stock->best_bid) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Limit price too high. Your price: %.2f, Best Bid: %.2f.", price, stock->best_bid);
        send_error(client_socket, request->header.request_id, msg);
        goto cleanup;
    }

    double total_proceeds = quantity * exec_price;

    // 5. Update user and stock data
    if (!portfolio_db_remove_holding(session->user_id, stock_id, quantity)) {
        send_error(client_socket, request->header.request_id, "Server error: Could not update portfolio.");
        goto cleanup;
    }

    double current_balance = account_db_get_balance(session->user_id);
    if (!account_db_update_balance(session->user_id, current_balance + total_proceeds)) {
        // Attempt to roll back portfolio change
        portfolio_db_add_holding(session->user_id, stock_id, quantity, exec_price);
        send_error(client_socket, request->header.request_id, "Server error: Could not update balance.");
        goto cleanup;
    }

    stock_db_update_volume(stock_id, stock->volume - quantity); // This is debatable, but for v1 let's assume it goes back to market

    // 6. Send response
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), "Order Filled: Sold %u %s at $%.2f.", quantity, stock->symbol, exec_price);
    
    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_SELL_STOCK_SUCCESS, success_msg);
    send_packet(client_socket, &response);

    printf("[SELL] Success: User %u sold %u %s\n", session->user_id, quantity, stock->symbol);

cleanup:
    stock_db_free(stock);
    portfolio_db_free(portfolio);
}