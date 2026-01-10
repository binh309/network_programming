#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "../network/protocol.h"
#include "../core/session_manager.h"
#include "../core/portfolio_manager.h"
#include "../data/account_db.h"
#include "../data/stock_db.h"
#include "../model/portfolio.h"
#include "../data/transaction_db.h"
#include "buy_stock.h"

void handle_buy_stock_request(int client_fd, struct buy_stock_request* request) {
    printf("[BUY_STOCK] Received request: stock_id=%u, qty=%u, price=%.2f\n",
           request->stock_id, request->quantity, request->price_per_unit);

    struct buy_stock_response resp;
    memset(&resp, 0, sizeof(resp));
    
    stock_t* stock = NULL;

    // Step 1: Check authentication
    uint32_t user_id = 0;
    printf("[BUY_STOCK] Checking authentication for client fd=%d\n", client_fd);
    if (!session_mgr_is_authenticated(client_fd, &user_id)) {
        resp.status = STATUS_NOT_AUTHENTICATED;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Not authenticated. Login first");
        printf("[BUY_STOCK] Error: Not authenticated\n");
        goto send_response;
    }

    printf("[BUY_STOCK] Client authenticated as user %u\n", user_id);

    // Step 2: Validate stock exists
    stock = stock_db_get_by_id(request->stock_id);
    if (!stock) {
        resp.status = STATUS_STOCK_NOT_FOUND;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Stock not found");
        printf("[BUY_STOCK] Error: Stock %u not found\n", request->stock_id);
        goto send_response;
    }

    // Step 3: Calculate total cost
    double total_cost = request->quantity * request->price_per_unit;

    // Step 4: Check user balance
    double balance = account_db_get_balance(user_id);
    if (balance < total_cost) {
        resp.status = STATUS_INSUFFICIENT_BALANCE;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Insufficient balance. Need $%.2f, have $%.2f",
                 total_cost, balance);
        printf("[BUY_STOCK] Error: Insufficient balance (need %.2f, have %.2f)\n", total_cost, balance);
        goto send_response;
    }

    // Step 5: Check stock availability
    if (stock->available_quantity < request->quantity) {
        resp.status = STATUS_INSUFFICIENT_STOCK;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Insufficient stock available. Need %u, have %u",
                 request->quantity, stock->available_quantity);
        printf("[BUY_STOCK] Error: Insufficient stock (need %u, have %u)\n",
               request->quantity, stock->available_quantity);
        goto send_response;
    }

    // Step 6: Execute purchase
    // Deduct money from user balance
    double new_balance = balance - total_cost;
    if (account_db_update_balance(user_id, new_balance) < 0) {
        resp.status = STATUS_SERVER_ERROR;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Server error: Failed to update balance");
        printf("[BUY_STOCK] Error: Failed to update balance\n");
        goto send_response;
    }

    // Reduce stock availability
    stock_db_update_quantity(request->stock_id, stock->available_quantity - request->quantity);

    // Create portfolio for user (if not exists) and add holding
    portfolio_t* portfolio = portfolio_mgr_get_or_create(user_id);
    if (!portfolio) {
        resp.status = STATUS_SERVER_ERROR;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Server error: Failed to get portfolio");
        printf("[BUY_STOCK] Error: Failed to get portfolio\n");
        goto send_response;
    }

    if (portfolio_add_holding(portfolio, request->stock_id, request->quantity, request->price_per_unit) < 0) {
        resp.status = STATUS_SERVER_ERROR;
        resp.order_id = (uint32_t)-1;
        snprintf(resp.message, 256, "Server error: Failed to add holding");
        printf("[BUY_STOCK] Error: Failed to add holding\n");
        goto send_response;
    }

    // Record transaction
    uint32_t order_id = transaction_db_record(user_id, TRANSACTION_BUY, request->stock_id,
                                             request->quantity, request->price_per_unit);

    resp.status = STATUS_SUCCESS;
    resp.order_id = order_id;
    snprintf(resp.message, 256, "Successfully bought %u %s @ $%.2f. Order ID: %u",
             request->quantity, stock->symbol, request->price_per_unit, order_id);

    printf("[BUY_STOCK] Success: User %u bought %u %s for $%.2f (Order ID: %u, New balance: $%.2f)\n",
           user_id, request->quantity, stock->symbol, total_cost, order_id, new_balance);

send_response:
    // Send response
    struct packet_header resp_hdr = {
        .type = MSG_BUY_STOCK_RESPONSE,
        .length = sizeof(resp)
    };
    
    resp.message_length = strlen(resp.message);

    printf("[BUY_STOCK] Sending response: status=%u, order_id=%u\n", resp.status, resp.order_id);
    
    // Send header + payload together
    char buffer[sizeof(struct packet_header) + sizeof(resp)];
    memcpy(buffer, &resp_hdr, sizeof(resp_hdr));
    memcpy(buffer + sizeof(resp_hdr), &resp, sizeof(resp));
    send(client_fd, buffer, sizeof(buffer), 0);
    
    printf("[BUY_STOCK] Response sent\n");

    if (stock) {
        stock_db_free(stock);
    }
}
