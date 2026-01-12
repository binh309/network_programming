#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buy_stock.h"
#include "../data/stock_db.h"
#include "../data/account_db.h"
#include "../data/portfolio_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

#define MAX_SHARES_PER_STOCK 500
#define MAX_TOTAL_SHARES 5000

// Handle buy stock request
void handle_buy_stock_request(int client_socket, const packet_t* request, connection_t* connection) {
    // 1. Parse request: "STOCK_ID,QUANTITY,PRICE,TYPE" (TYPE is LIMIT or MARKET)
    uint16_t stock_id;
    uint32_t quantity;
    double price;
    char type[10] = {0};

    if (sscanf(request->body, "%hu,%u,%lf,%9s", &stock_id, &quantity, &price, type) != 4) {
        send_error(client_socket, request->header.request_id, "Invalid buy request format. Use: STOCK_ID,QTY,PRICE,TYPE");
        return;
    }
    
    printf("[BUY] User %u wants to buy %u of stock %hu at %.2f (%s)\n", connection->user_id, quantity, stock_id, price, type);

    // 2. Get stock and user data
    stock_t* stock = stock_db_get_by_id(stock_id);
    if (!stock) {
        send_error(client_socket, request->header.request_id, "Stock not found.");
        return;
    }

    double user_balance = account_db_get_balance(connection->user_id);
    portfolio_t* portfolio = portfolio_db_get(connection->user_id);

    // Determine execution price
    bool is_market_order = (strcmp(type, "MARKET") == 0);
    double exec_price = is_market_order ? stock->last_price : price;
    
    // For LIMIT orders, check if the price is valid for an immediate fill
    if (!is_market_order && price < stock->best_ask) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Limit price too low. Your price: %.2f, Best Ask: %.2f.", price, stock->best_ask);
        send_error(client_socket, request->header.request_id, msg);
        stock_db_free(stock);
        if (portfolio) portfolio_db_free(portfolio);
        return;
    }

    double total_cost = quantity * exec_price;

    // 3. Validate order
    if (user_balance < total_cost) {
        send_error(client_socket, request->header.request_id, "Insufficient balance.");
        goto cleanup;
    }

    // Risk Check 1: Max shares per stock
    uint32_t current_holding = 0;
    if (portfolio) {
        for (uint32_t i = 0; i < portfolio->holding_count; ++i) {
            if (portfolio->holdings[i].stock_id == stock_id) {
                current_holding = portfolio->holdings[i].quantity;
                break;
            }
        }
    }
    if (current_holding + quantity > MAX_SHARES_PER_STOCK) {
        send_error(client_socket, request->header.request_id, "Risk limit: Exceeds max shares per stock.");
        goto cleanup;
    }

    // Risk Check 2: Max total shares
    uint32_t total_shares = 0;
    if (portfolio) {
        for (uint32_t i = 0; i < portfolio->holding_count; ++i) {
            total_shares += portfolio->holdings[i].quantity;
        }
    }
    if (total_shares + quantity > MAX_TOTAL_SHARES) {
        send_error(client_socket, request->header.request_id, "Risk limit: Exceeds max total shares.");
        goto cleanup;
    }

    // 4. Process order
    // In V1, we assume instant fill if conditions are met.
    
    // 5. Update user and stock data
    if (!account_db_update_balance(connection->user_id, user_balance - total_cost)) {
        send_error(client_socket, request->header.request_id, "Server error: Could not update balance.");
        goto cleanup;
    }

    if (!portfolio_db_add_holding(connection->user_id, stock_id, quantity, exec_price)) {
        // Attempt to roll back the balance deduction
        account_db_update_balance(connection->user_id, user_balance);
        send_error(client_socket, request->header.request_id, "Server error: Could not update portfolio.");
        goto cleanup;
    }
    
    stock_db_update_volume(stock_id, stock->volume + quantity);

    // 6. Send response
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), "Order Filled: Bought %u %s at $%.2f.", quantity, stock->symbol, exec_price);
    
    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_BUY_STOCK_SUCCESS, success_msg);
    send_packet(client_socket, &response);
    
    printf("[BUY] Success: User %u bought %u %s\n", connection->user_id, quantity, stock->symbol);

cleanup:
    stock_db_free(stock);
    if (portfolio) portfolio_db_free(portfolio);
}