#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sell_stock.h"
#include "../data/stock_db.h"
#include "../data/account_db.h"
#include "../data/portfolio_db.h"
#include "../data/transaction_db.h"
#include "../core/portfolio_manager.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"
#include "../ui/tui.h"
#include "../ui/stats.h"

// Handle sell stock request
void handle_sell_stock_request(int client_socket, const packet_t* request, connection_t* connection) {
    // Start timing for latency measurement
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    if (!connection->is_logged_in) {
        send_error(client_socket, request->header.request_id, "You must be logged in to sell stocks.");
        return;
    }

    // 1. Parse request: "STOCK_ID,QUANTITY,LIMIT_PRICE,TYPE"
    uint16_t stock_id;
    uint32_t quantity;
    double limit_price;
    char type[10] = {0};

    if (sscanf(request->body, "%hu,%u,%lf,%9s", &stock_id, &quantity, &limit_price, type) != 4) {
        send_error(client_socket, request->header.request_id, "Invalid sell request format. Use: STOCK_ID,QTY,PRICE,TYPE");
        return;
    }

    server_debug("[SELL] User %u wants to sell %u of stock %hu at %.2f (%s)\n", 
           connection->user_id, quantity, stock_id, limit_price, type);

    // ========== CRITICAL FIX #4: INPUT VALIDATION ==========
    // Validate quantity
    if (quantity == 0) {
        send_error(client_socket, request->header.request_id, "Quantity must be greater than zero.");
        return;
    }
    if (quantity > 1000000) {
        send_error(client_socket, request->header.request_id, "Quantity exceeds maximum allowed (1,000,000).");
        return;
    }

    // Validate limit price (BEFORE locking anything)
    if (limit_price <= 0.0) {
        send_error(client_socket, request->header.request_id, "Price must be positive.");
        return;
    }
    if (limit_price > 999999.99) {
        send_error(client_socket, request->header.request_id, "Price exceeds maximum allowed ($999,999.99).");
        return;
    }

    // Validate order type
    bool is_market_order = (strcmp(type, "MARKET") == 0);
    bool is_limit_order = (strcmp(type, "LIMIT") == 0);
    if (!is_market_order && !is_limit_order) {
        send_error(client_socket, request->header.request_id, "Order type must be MARKET or LIMIT.");
        return;
    }

    // 2. Get stock and user data
    stock_t* stock = stock_db_get_by_id(stock_id);
    if (!stock) {
        send_error(client_socket, request->header.request_id, "Stock not found.");
        return;
    }

    // ========== CRITICAL FIX #0: USE PERSISTENT PORTFOLIO MANAGER ==========
    // BUG FIX: Was using portfolio_db_get() which created temporary portfolio
    // Now: Use portfolio_mgr_get_or_create() for persistent storage across requests
    // This fixes the "user can't sell stocks they own" bug
    portfolio_t* portfolio = portfolio_mgr_get_or_create(connection->user_id);
    if (!portfolio) {
        send_error(client_socket, request->header.request_id, "Could not retrieve portfolio.");
        stock_db_free(stock);
        return;
    }

    // ========== CRITICAL FIX #1: VALIDATE PRICE AGAINST MARKET ==========
    // For SELL orders, we execute at market BID (not client's limit price)
    double market_bid_price = stock->best_bid;
    double execution_price;

    if (is_market_order) {
        // Market order: Execute at current market bid price
        execution_price = market_bid_price;
    } else {
        // LIMIT order: Client willing to sell at least limit_price
        // But execute at best available price (market bid)
        if (limit_price > market_bid_price) {
            char msg[256];
            snprintf(msg, sizeof(msg), 
                    "Limit price (%.2f) is above current bid (%.2f). Order rejected.", 
                    limit_price, market_bid_price);
            send_error(client_socket, request->header.request_id, msg);
            stock_db_free(stock);
            // NOTE: Don't free portfolio - it's managed by portfolio_manager
            return;
        }
        // Execute at market bid (better price than limit for seller)
        execution_price = market_bid_price;
    }

    server_debug("[SELL] Execution price: %.2f (Market bid: %.2f, Client limit: %.2f)\n",
           execution_price, market_bid_price, limit_price);

    // 3. Validate order
    uint32_t current_holding = 0;
    for (uint32_t i = 0; i < portfolio->holding_count; ++i) {
        if (portfolio->holdings[i].stock_id == stock_id) {
            current_holding = portfolio->holdings[i].quantity;
            break;
        }
    }

    if (current_holding < quantity) {
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double latency_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                            (end_time.tv_nsec - start_time.tv_nsec) / 1e6;
        stats_record_failed_order(latency_ms, REJECT_INSUFFICIENT_HOLDINGS);
        tui_log(LOG_WARNING, "SELL REJECTED: Insufficient holdings for %s", connection->username);
        send_error(client_socket, request->header.request_id, "Insufficient holdings to sell.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }

    double total_proceeds = quantity * execution_price;

    // ========== CRITICAL FIX #3: TRANSACTION ROLLBACK ==========
    // Save original state so we can rollback if any operation fails
    double old_balance = account_db_get_balance(connection->user_id);

    server_debug("[SELL] Transaction state saved for rollback if needed\n");
    server_debug("[SELL]   Old balance: %.2f\n", old_balance);

    // 4. Process order - ATOMIC OPERATIONS
    // Step 1: Remove from portfolio
    if (!portfolio_db_remove_holding(connection->user_id, stock_id, quantity)) {
        send_error(client_socket, request->header.request_id, 
                  "Server error: Could not update portfolio.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }
    server_debug("[SELL] Step 1 OK: Portfolio updated (-%u shares)\n", quantity);

    // Step 2: Update balance
    if (!account_db_update_balance(connection->user_id, old_balance + total_proceeds)) {
        // ROLLBACK: Restore portfolio
        server_debug("[SELL] Step 2 FAILED: Balance update failed. Rolling back...\n");
        portfolio_db_add_holding(connection->user_id, stock_id, quantity, execution_price);
        server_debug("[SELL] Rollback: Portfolio restored\n");
        send_error(client_socket, request->header.request_id, 
                  "Server error: Could not update balance. Transaction rolled back.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }
    server_debug("[SELL] Step 2 OK: Balance updated (+%.2f)\n", total_proceeds);

    // Step 3: Update stock volume using ATOMIC operation (add back to market)
    uint32_t new_volume;
    if (!stock_db_atomic_sell(stock_id, quantity, &new_volume)) {
        // ROLLBACK: Restore both portfolio and balance
        server_debug("[SELL] Step 3 FAILED: Atomic stock sell failed. Rolling back...\n");
        portfolio_db_add_holding(connection->user_id, stock_id, quantity, execution_price);
        account_db_update_balance(connection->user_id, old_balance);
        server_debug("[SELL] Rollback: Portfolio restored, balance reverted\n");
        send_error(client_socket, request->header.request_id, 
                  "Server error: Could not update stock. Transaction rolled back.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }
    server_debug("[SELL] Step 3 OK: Stock volume updated atomically (new volume: %u)\n", new_volume);

    // Step 4: Record transaction (audit trail)
    uint32_t order_id = transaction_db_record(connection->user_id, TRANSACTION_SELL, 
                                              stock_id, quantity, execution_price);
    if (order_id == (uint32_t)-1) {
        server_debug("[SELL] WARNING: Transaction recording failed (audit trail)\n");
    }
    server_debug("[SELL] Step 4 OK: Transaction recorded (Order ID: %u)\n", order_id);

    // CRITICAL FIX: Reload portfolio in portfolio_manager to sync with portfolio_db
    // This ensures the next request sees the updated holdings
    portfolio_mgr_reload_user(connection->user_id);

    // Record stats for TUI with actual latency and trade value
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double latency_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                        (end_time.tv_nsec - start_time.tv_nsec) / 1e6;
    double trade_value = quantity * execution_price;
    stats_record_order(false, latency_ms, stock_id, connection->user_id, trade_value);
    tui_log(LOG_SUCCESS, "SELL %u %s @ $%.2f by %s", quantity, stock->symbol, execution_price, connection->username);

    // ========== PRICE IMPACT: Supply pushes price DOWN ==========
    // Simulates supply/demand: selling increases supply, decreases bid price
    // Impact: ~0.01% per 100 shares traded
    double impact = 0.0001 * quantity;
    if (impact > 0.05) impact = 0.05;  // Cap at 5% max impact per trade
    double spread = stock->best_ask - stock->best_bid;
    double new_bid = execution_price * (1.0 - impact);
    double new_ask = new_bid + spread;
    double new_last = execution_price;
    stock_db_update_price(stock_id, new_bid, new_ask, new_last);
    server_debug("[SELL] Price impact: bid %.2f -> %.2f (-%.2f%%)\n", 
           execution_price, new_bid, impact * 100);

    // 5. Send response with ACTUAL execution details (not client-provided price)
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), 
            "Order Filled: Sold %u %s at $%.2f (limit was $%.2f). Order ID: %u", 
            quantity, stock->symbol, execution_price, limit_price, order_id);

    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_SELL_STOCK_SUCCESS, success_msg);
    send_packet(client_socket, &response);

    server_debug("[SELL] ✓ SUCCESS: User %u sold %u %s\n", connection->user_id, quantity, stock->symbol);
    server_debug("[SELL] Execution: %.2f per share, Total proceeds: $%.2f\n", execution_price, total_proceeds);

    stock_db_free(stock);
    // NOTE: Don't free portfolio - it's managed by portfolio_manager
}
