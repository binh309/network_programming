#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "../network/protocol.h"
#include "../core/session_manager.h"
#include "../data/account_db.h"
#include "../data/stock_db.h"
#include "../model/portfolio.h"
#include "../data/transaction_db.h"
#include "../core/portfolio_manager.h"
#include "sell_stock.h"

void handle_sell_stock_request(int client_fd, struct sell_stock_request* request) {
    printf("[SELL_STOCK] Received request: stock_id=%u, qty=%u, price=%.2f\n",
           request->stock_id, request->quantity, request->price_per_unit);

    struct sell_stock_response resp;
    memset(&resp, 0, sizeof(resp));
    
    stock_t* stock = NULL;

    // Step 1: Check authentication
    uint32_t user_id = 0;
    printf("[SELL_STOCK] Checking authentication for client fd=%d\n", client_fd);
    if (!session_mgr_is_authenticated(client_fd, &user_id)) {
        resp.status = STATUS_NOT_AUTHENTICATED;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Not authenticated. Login first");
        printf("[SELL_STOCK] Error: Not authenticated\n");
        goto send_response;
    }

    printf("[SELL_STOCK] Client authenticated as user %u\n", user_id);

    // Step 2: Validate stock exists
    stock = stock_db_get_by_id(request->stock_id);
    if (!stock) {
        resp.status = STATUS_STOCK_NOT_FOUND;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Stock not found");
        printf("[SELL_STOCK] Error: Stock %u not found\n", request->stock_id);
        goto send_response;
    }

    // Step 3: Get user portfolio and check holding
    portfolio_t* portfolio = portfolio_mgr_get_or_create(user_id);
    if (!portfolio) {
        resp.status = STATUS_SERVER_ERROR;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Server error: Failed to get portfolio");
        printf("[SELL_STOCK] Error: Failed to create portfolio\n");
        goto send_response;
    }

    holding_t* holding = portfolio_find_holding(portfolio, request->stock_id);
    if (!holding) {
        resp.status = STATUS_STOCK_NOT_IN_PORTFOLIO;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "You don't own this stock");
        printf("[SELL_STOCK] Error: User doesn't own stock %u\n", request->stock_id);
        goto send_response;
    }

    // Step 4: Check sufficient holdings
    if (holding->quantity < request->quantity) {
        resp.status = STATUS_INSUFFICIENT_HOLDINGS;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Insufficient holdings. Need %u, have %u",
                 request->quantity, holding->quantity);
        printf("[SELL_STOCK] Error: Insufficient holdings (need %u, have %u)\n",
               request->quantity, holding->quantity);
        goto send_response;
    }

    // Step 5: Calculate sale proceeds
    double proceeds = request->quantity * request->price_per_unit;

    // Step 6: Execute sale
    // Add money to user balance
    double balance = account_db_get_balance(user_id);
    double new_balance = balance + proceeds;
    if (account_db_update_balance(user_id, new_balance) < 0) {
        resp.status = STATUS_SERVER_ERROR;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Server error: Failed to update balance");
        printf("[SELL_STOCK] Error: Failed to update balance\n");
        portfolio_free(portfolio);
        goto send_response;
    }

    // Remove shares from portfolio
    int remove_result = portfolio_remove_holding(portfolio, request->stock_id, request->quantity);
    if (remove_result < 0) {
        resp.status = STATUS_SERVER_ERROR;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Server error: Failed to remove holdings");
        printf("[SELL_STOCK] Error: Failed to remove holdings\n");
        portfolio_free(portfolio);
        goto send_response;
    }

    // Return stock to market
    stock_db_update_quantity(request->stock_id, stock->available_quantity + request->quantity);

    // Record transaction
    uint32_t order_id = transaction_db_record(user_id, TRANSACTION_SELL, request->stock_id,
                                             request->quantity, request->price_per_unit);

    resp.status = STATUS_SUCCESS;
    resp.order_id = order_id;
    snprintf(resp.message, 256, "Successfully sold %u %s @ $%.2f. Order ID: %u",
             request->quantity, stock->symbol, request->price_per_unit, order_id);

    printf("[SELL_STOCK] Success: User %u sold %u %s for $%.2f (Order ID: %u, New balance: $%.2f)\n",
           user_id, request->quantity, stock->symbol, proceeds, order_id, new_balance);

send_response:
    // Send response
    struct packet_header resp_hdr = {
        .type = MSG_SELL_STOCK_RESPONSE,
        .length = sizeof(resp)
    };
    
    resp.message_length = strlen(resp.message);

    printf("[SELL_STOCK] Sending response: status=%u, order_id=%u\n", resp.status, resp.order_id);
    
    // Send header + payload together
    char buffer[sizeof(struct packet_header) + sizeof(resp)];
    memcpy(buffer, &resp_hdr, sizeof(resp_hdr));
    memcpy(buffer + sizeof(resp_hdr), &resp, sizeof(resp));
    send(client_fd, buffer, sizeof(buffer), 0);
    
    printf("[SELL_STOCK] Response sent\n");

    if (stock) {
        stock_db_free(stock);
    }
}
