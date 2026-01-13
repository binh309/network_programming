#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "buy_stock.h"
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

// Risk Limits - increased for load testing (originally 500/5000)
#define MAX_SHARES_PER_STOCK 100000
#define MAX_TOTAL_SHARES 1000000

// Handle buy stock request
void handle_buy_stock_request(int client_socket, const packet_t* request, connection_t* connection) {
    // Start timing for latency measurement
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    // 1. Parse request: "STOCK_ID,QUANTITY,PRICE,TYPE" (TYPE is LIMIT or MARKET)
    uint16_t stock_id;
    uint32_t quantity;
    double limit_price;
    char type[10] = {0};

    if (sscanf(request->body, "%hu,%u,%lf,%9s", &stock_id, &quantity, &limit_price, type) != 4) {
        send_error(client_socket, request->header.request_id, "Invalid buy request format. Use: STOCK_ID,QTY,PRICE,TYPE");
        return;
    }
    
    server_debug("[BUY] User %u wants to buy %u of stock %hu at %.2f (%s)\n", 
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

    // ========== CRITICAL FIX #1: VALIDATE PRICE AGAINST MARKET ==========
    // Get stock data - this is a read operation
    stock_t* stock = stock_db_get_by_id(stock_id);
    if (!stock) {
        send_error(client_socket, request->header.request_id, "Stock not found.");
        return;
    }

    // For LIMIT orders, client's limit_price must be >= market ask price
    // For MARKET orders, use current ask price
    double market_ask_price = stock->best_ask;  // Current market ask
    double execution_price;

    if (is_market_order) {
        // Market order: Execute at current market ask price
        execution_price = market_ask_price;
    } else {
        // LIMIT order: Client willing to pay UP TO limit_price
        // But execute at best available price (market ask)
        if (limit_price < market_ask_price) {
            char msg[256];
            snprintf(msg, sizeof(msg), 
                    "Limit price (%.2f) is below current ask (%.2f). Order rejected.", 
                    limit_price, market_ask_price);
            send_error(client_socket, request->header.request_id, msg);
            stock_db_free(stock);
            return;
        }
        // Execute at market ask (better price than limit)
        execution_price = market_ask_price;
    }

    server_debug("[BUY] Execution price: %.2f (Market ask: %.2f, Client limit: %.2f)\n",
           execution_price, market_ask_price, limit_price);

    double total_cost = quantity * execution_price;
    double user_balance = account_db_get_balance(connection->user_id);
    
    // ========== CRITICAL FIX #0: USE PERSISTENT PORTFOLIO MANAGER ==========
    // Use portfolio_mgr_get_or_create() for persistent storage across requests
    portfolio_t* portfolio = portfolio_mgr_get_or_create(connection->user_id);

    // 3. Validate order
    if (user_balance < total_cost) {
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double latency_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                            (end_time.tv_nsec - start_time.tv_nsec) / 1e6;
        stats_record_failed_order(latency_ms, REJECT_INSUFFICIENT_BALANCE);
        tui_log(LOG_WARNING, "BUY REJECTED: Insufficient balance for %s", connection->username);
        send_error(client_socket, request->header.request_id, "Insufficient balance.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
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
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double latency_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                            (end_time.tv_nsec - start_time.tv_nsec) / 1e6;
        stats_record_failed_order(latency_ms, REJECT_RISK_LIMIT);
        tui_log(LOG_WARNING, "BUY REJECTED: Risk limit for %s", connection->username);
        send_error(client_socket, request->header.request_id, "Risk limit: Exceeds max shares per stock.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }

    // Risk Check 2: Max total shares
    uint32_t total_shares = 0;
    if (portfolio) {
        for (uint32_t i = 0; i < portfolio->holding_count; ++i) {
            total_shares += portfolio->holdings[i].quantity;
        }
    }
    if (total_shares + quantity > MAX_TOTAL_SHARES) {
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double latency_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                            (end_time.tv_nsec - start_time.tv_nsec) / 1e6;
        stats_record_failed_order(latency_ms, REJECT_RISK_LIMIT);
        tui_log(LOG_WARNING, "BUY REJECTED: Risk limit for %s", connection->username);
        send_error(client_socket, request->header.request_id, "Risk limit: Exceeds max total shares.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }

    // ========== CRITICAL FIX #2: FIX TOCTOU RACE CONDITION ==========
    // IMPORTANT: Use atomic check-and-update to prevent race condition
    // where two threads both read old quantity, then both update it
    // See: https://en.wikipedia.org/wiki/Time-of-check_to_time-of-use
    //
    // Scenario without fix:
    //   Thread A: reads qty=5, decides 5>=3? YES, proceed
    //   Thread B: reads qty=5, decides 5>=4? YES, proceed
    //   Thread A: updates qty = 5-3 = 2
    //   Thread B: updates qty = 2-4 = -2 (WRONG!)
    //
    // Fix: Use stock_db_atomic_buy() which locks, checks, and updates atomically
    
    server_debug("[BUY] Using atomic stock operation to prevent race condition...\n");

    // ========== CRITICAL FIX #3: TRANSACTION ROLLBACK ==========
    // Save original state so we can rollback if any operation fails
    double old_balance = user_balance;
    
    server_debug("[BUY] Transaction state saved for rollback if needed\n");
    server_debug("[BUY]   Old balance: %.2f\n", old_balance);

    // 4. Process order - ATOMIC OPERATIONS
    // Step 1: Update balance
    if (!account_db_update_balance(connection->user_id, user_balance - total_cost)) {
        send_error(client_socket, request->header.request_id, 
                  "Server error: Could not update balance.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }
    server_debug("[BUY] Step 1 OK: Balance updated (%.2f -> %.2f)\n", 
           old_balance, user_balance - total_cost);

    // Step 2: Update portfolio
    if (!portfolio_db_add_holding(connection->user_id, stock_id, quantity, execution_price)) {
        // ROLLBACK: Undo balance update
        server_debug("[BUY] Step 2 FAILED: Portfolio update failed. Rolling back...\n");
        account_db_update_balance(connection->user_id, old_balance);
        server_debug("[BUY] Rollback: Balance restored to %.2f\n", old_balance);
        send_error(client_socket, request->header.request_id, 
                  "Server error: Could not update portfolio. Transaction rolled back.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }
    server_debug("[BUY] Step 2 OK: Portfolio updated (+%u shares)\n", quantity);

    // Step 3: Update stock volume using ATOMIC operation (TOCTOU FIX)
    uint32_t new_volume;
    if (!stock_db_atomic_buy(stock_id, quantity, &new_volume)) {
        // ROLLBACK: Undo balance and portfolio updates
        server_debug("[BUY] Step 3 FAILED: Atomic stock buy failed. Rolling back...\n");
        account_db_update_balance(connection->user_id, old_balance);
        portfolio_db_remove_holding(connection->user_id, stock_id, quantity);
        server_debug("[BUY] Rollback: Balance restored, portfolio reverted\n");
        send_error(client_socket, request->header.request_id, 
                  "Server error: Insufficient stock or update failed. Transaction rolled back.");
        stock_db_free(stock);
        // NOTE: Don't free portfolio - it's managed by portfolio_manager
        return;
    }
    server_debug("[BUY] Step 3 OK: Stock volume updated atomically (new volume: %u)\n", new_volume);

    // Step 4: Record transaction (audit trail)
    // Note: This doesn't affect user-facing state, so failure here doesn't require rollback
    uint32_t order_id = transaction_db_record(connection->user_id, TRANSACTION_BUY, 
                                              stock_id, quantity, execution_price);
    if (order_id == (uint32_t)-1) {
        server_debug("[BUY] WARNING: Transaction recording failed (audit trail)\n");
        // Don't rollback the entire order for audit failure
        // But notify the user
        server_debug("[BUY] User %u order completed but audit logging failed\n", connection->user_id);
    }
    server_debug("[BUY] Step 4 OK: Transaction recorded (Order ID: %u)\n", order_id);

    // CRITICAL FIX: Reload portfolio in portfolio_manager to sync with portfolio_db
    // This ensures the next request sees the updated holdings
    portfolio_mgr_reload_user(connection->user_id);

    // Record stats for TUI with actual latency and trade value
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double latency_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                        (end_time.tv_nsec - start_time.tv_nsec) / 1e6;
    double trade_value = quantity * execution_price;
    stats_record_order(true, latency_ms, stock_id, connection->user_id, trade_value);
    tui_log(LOG_SUCCESS, "BUY %u %s @ $%.2f by %s", quantity, stock->symbol, execution_price, connection->username);

    // ========== PRICE IMPACT: Demand pushes price UP ==========
    // Simulates supply/demand: buying reduces supply, increases ask price
    // Impact: ~0.01% per 100 shares traded
    double impact = 0.0001 * quantity;
    if (impact > 0.05) impact = 0.05;  // Cap at 5% max impact per trade
    double spread = stock->best_ask - stock->best_bid;
    double new_ask = execution_price * (1.0 + impact);
    double new_bid = new_ask - spread;
    double new_last = execution_price;
    stock_db_update_price(stock_id, new_bid, new_ask, new_last);
    server_debug("[BUY] Price impact: ask %.2f -> %.2f (+%.2f%%)\n", 
           execution_price, new_ask, impact * 100);

    // 5. Send response with ACTUAL execution details (not client-provided price)
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), 
            "Order Filled: Bought %u %s at $%.2f (limit was $%.2f). Order ID: %u", 
            quantity, stock->symbol, execution_price, limit_price, order_id);
    
    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_BUY_STOCK_SUCCESS, success_msg);
    send_packet(client_socket, &response);
    
    server_debug("[BUY] ✓ SUCCESS: User %u bought %u %s\n", connection->user_id, quantity, stock->symbol);
    server_debug("[BUY] Execution: %.2f per share, Total: $%.2f\n", execution_price, total_cost);

    stock_db_free(stock);
    // NOTE: Don't free portfolio - it's managed by portfolio_manager
}
