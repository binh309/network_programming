/**
 * admin.c - Admin Command Interface Implementation
 * 
 * Uses only existing database APIs for admin operations.
 */

#include "admin.h"
#include "stats.h"
#include "../data/account_db.h"
#include "../data/stock_db.h"
#include "../data/portfolio_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Global history instance
admin_history_t g_admin_history;

int admin_init(void) {
    memset(&g_admin_history, 0, sizeof(admin_history_t));
    g_admin_history.current = -1;
    return 0;
}

void admin_cleanup(void) {
    // Nothing to cleanup for in-memory history
}

void admin_history_add(const char* command) {
    if (!command || strlen(command) == 0) return;
    
    // Don't add duplicates of the last command
    if (g_admin_history.count > 0) {
        int last_idx = (g_admin_history.count - 1) % ADMIN_HISTORY_SIZE;
        if (strcmp(g_admin_history.commands[last_idx], command) == 0) {
            return;
        }
    }
    
    int idx = g_admin_history.count % ADMIN_HISTORY_SIZE;
    strncpy(g_admin_history.commands[idx], command, ADMIN_CMD_MAX_LEN - 1);
    g_admin_history.commands[idx][ADMIN_CMD_MAX_LEN - 1] = '\0';
    g_admin_history.count++;
    g_admin_history.current = g_admin_history.count;
}

const char* admin_history_prev(void) {
    if (g_admin_history.count == 0) return NULL;
    
    if (g_admin_history.current > 0) {
        g_admin_history.current--;
    }
    
    int start = 0;
    if (g_admin_history.count > ADMIN_HISTORY_SIZE) {
        start = g_admin_history.count - ADMIN_HISTORY_SIZE;
    }
    
    if (g_admin_history.current < start) {
        g_admin_history.current = start;
    }
    
    int idx = g_admin_history.current % ADMIN_HISTORY_SIZE;
    return g_admin_history.commands[idx];
}

const char* admin_history_next(void) {
    if (g_admin_history.count == 0) return NULL;
    
    if (g_admin_history.current < g_admin_history.count - 1) {
        g_admin_history.current++;
        int idx = g_admin_history.current % ADMIN_HISTORY_SIZE;
        return g_admin_history.commands[idx];
    }
    
    g_admin_history.current = g_admin_history.count;
    return "";  // Return empty for new command
}

void admin_history_reset_nav(void) {
    g_admin_history.current = g_admin_history.count;
}

// Helper to trim whitespace
static char* trim(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Forward declaration
void admin_get_help(char* buffer, size_t size);

// Command handlers
static bool cmd_help(admin_result_t* result) {
    admin_get_help(result->message, sizeof(result->message));
    result->success = true;
    return true;
}

static bool cmd_status(admin_result_t* result) {
    char uptime[64];
    stats_get_uptime(uptime, sizeof(uptime));
    
    snprintf(result->message, sizeof(result->message),
        "SERVER STATUS\n"
        "=========================================\n"
        "Uptime:              %s\n"
        "Active Connections:  %u\n"
        "Total Connections:   %u\n"
        "Total Orders:        %lu\n"
        "Buy Orders:          %lu\n"
        "Sell Orders:         %lu\n"
        "Failed Orders:       %lu\n"
        "Success Rate:        %.2f%%\n"
        "Orders/sec:          %.2f\n"
        "Avg Latency:         %.2f ms\n"
        "Memory Usage:        %.2f MB\n",
        uptime,
        g_stats.active_connections,
        g_stats.total_connections,
        g_stats.total_orders,
        g_stats.buy_orders,
        g_stats.sell_orders,
        g_stats.failed_orders,
        g_stats.success_rate,
        g_stats.orders_per_sec,
        g_stats.latency_avg,
        g_stats.memory_usage / (1024.0 * 1024.0)
    );
    result->success = true;
    return true;
}

static bool cmd_create_user(const char* args, admin_result_t* result) {
    char username[64], password[64];
    
    if (sscanf(args, "%63s %63s", username, password) != 2) {
        snprintf(result->message, sizeof(result->message),
            "Usage: create_user <username> <password>\n\n"
            "Note: New accounts start with $0 balance.\n"
            "Use 'credit' command to add funds.");
        result->success = false;
        return true;
    }
    
    // Check if username exists
    if (account_db_username_exists(username)) {
        snprintf(result->message, sizeof(result->message),
            "Error: Username '%s' already exists", username);
        result->success = false;
        return true;
    }
    
    // Create account
    if (!account_db_add(username, password)) {
        snprintf(result->message, sizeof(result->message),
            "Error: Failed to create user");
        result->success = false;
        return true;
    }
    
    snprintf(result->message, sizeof(result->message),
        "✓ Created user: %s\n"
        "  Initial Balance: $0.00\n"
        "  Use 'credit' to add funds",
        username);
    result->success = true;
    return true;
}

static bool cmd_credit(const char* args, admin_result_t* result) {
    int user_id;
    double amount;
    
    if (sscanf(args, "%d %lf", &user_id, &amount) != 2) {
        snprintf(result->message, sizeof(result->message),
            "Usage: credit <user_id> <amount>");
        result->success = false;
        return true;
    }
    
    if (amount <= 0) {
        snprintf(result->message, sizeof(result->message),
            "Error: Amount must be positive");
        result->success = false;
        return true;
    }
    
    double old_balance = account_db_get_balance(user_id);
    if (old_balance < 0) {
        snprintf(result->message, sizeof(result->message),
            "Error: User ID %d not found", user_id);
        result->success = false;
        return true;
    }
    
    double new_balance = old_balance + amount;
    if (!account_db_update_balance(user_id, new_balance)) {
        snprintf(result->message, sizeof(result->message),
            "Error: Failed to update balance");
        result->success = false;
        return true;
    }
    
    snprintf(result->message, sizeof(result->message),
        "✓ Credited $%.2f to user %d\n"
        "  Old Balance: $%.2f\n"
        "  New Balance: $%.2f",
        amount, user_id, old_balance, new_balance);
    result->success = true;
    return true;
}

static bool cmd_debit(const char* args, admin_result_t* result) {
    int user_id;
    double amount;
    
    if (sscanf(args, "%d %lf", &user_id, &amount) != 2) {
        snprintf(result->message, sizeof(result->message),
            "Usage: debit <user_id> <amount>");
        result->success = false;
        return true;
    }
    
    if (amount <= 0) {
        snprintf(result->message, sizeof(result->message),
            "Error: Amount must be positive");
        result->success = false;
        return true;
    }
    
    double old_balance = account_db_get_balance(user_id);
    if (old_balance < 0) {
        snprintf(result->message, sizeof(result->message),
            "Error: User ID %d not found", user_id);
        result->success = false;
        return true;
    }
    
    if (old_balance < amount) {
        snprintf(result->message, sizeof(result->message),
            "Error: Insufficient balance (has $%.2f, need $%.2f)",
            old_balance, amount);
        result->success = false;
        return true;
    }
    
    double new_balance = old_balance - amount;
    if (!account_db_update_balance(user_id, new_balance)) {
        snprintf(result->message, sizeof(result->message),
            "Error: Failed to update balance");
        result->success = false;
        return true;
    }
    
    snprintf(result->message, sizeof(result->message),
        "✓ Debited $%.2f from user %d\n"
        "  Old Balance: $%.2f\n"
        "  New Balance: $%.2f",
        amount, user_id, old_balance, new_balance);
    result->success = true;
    return true;
}

static bool cmd_list_users(admin_result_t* result) {
    char* msg = result->message;
    int remaining = sizeof(result->message);
    int written;
    
    written = snprintf(msg, remaining,
        "USER LIST\n"
        "=========================================\n"
        "[ID]  Username          Balance\n"
        "-----------------------------------------\n");
    msg += written;
    remaining -= written;
    
    // Get all accounts (limited display)
    for (uint32_t i = 1; i <= 50 && remaining > 100; i++) {
        account_t* account = account_db_get_by_id(i);
        if (account) {
            written = snprintf(msg, remaining, "[%2u]  %-16s  $%.2f\n",
                account->user_id, account->username, account->balance);
            msg += written;
            remaining -= written;
            account_db_free(account);
        }
    }
    
    result->success = true;
    return true;
}

static bool cmd_list_stocks(admin_result_t* result) {
    char* msg = result->message;
    int remaining = sizeof(result->message);
    int written;
    
    written = snprintf(msg, remaining,
        "STOCK LIST\n"
        "=======================================================\n"
        "[ID]  Symbol    Bid        Ask        Last       Volume\n"
        "-------------------------------------------------------\n");
    msg += written;
    remaining -= written;
    
    int stock_count = 0;
    stock_t* stocks = stock_db_get_all(&stock_count);
    
    if (stocks) {
        for (int i = 0; i < stock_count && remaining > 100; i++) {
            written = snprintf(msg, remaining, "[%2u]  %-8s  $%-8.2f  $%-8.2f  $%-8.2f  %u\n",
                stocks[i].stock_id, stocks[i].symbol, stocks[i].best_bid, stocks[i].best_ask,
                stocks[i].last_price, stocks[i].volume);
            msg += written;
            remaining -= written;
        }
        stock_db_free(stocks);
    }
    
    result->success = true;
    return true;
}

static bool cmd_set_price(const char* args, admin_result_t* result) {
    int stock_id;
    double bid, ask;
    
    if (sscanf(args, "%d %lf %lf", &stock_id, &bid, &ask) != 3) {
        snprintf(result->message, sizeof(result->message),
            "Usage: set_price <stock_id> <bid> <ask>");
        result->success = false;
        return true;
    }
    
    stock_t* stock = stock_db_get_by_id(stock_id);
    if (!stock) {
        snprintf(result->message, sizeof(result->message),
            "Error: Stock ID %d not found", stock_id);
        result->success = false;
        return true;
    }
    
    if (bid <= 0 || ask <= 0 || bid >= ask) {
        snprintf(result->message, sizeof(result->message),
            "Error: Invalid prices (bid must be positive and less than ask)");
        stock_db_free(stock);
        result->success = false;
        return true;
    }
    
    double old_bid = stock->best_bid;
    double old_ask = stock->best_ask;
    char symbol[16];
    strncpy(symbol, stock->symbol, sizeof(symbol));
    stock_db_free(stock);
    
    double last = (bid + ask) / 2;
    if (!stock_db_update_price(stock_id, bid, ask, last)) {
        snprintf(result->message, sizeof(result->message),
            "Error: Failed to update prices");
        result->success = false;
        return true;
    }
    
    snprintf(result->message, sizeof(result->message),
        "✓ Updated prices for %s (ID: %d)\n"
        "  Old: Bid $%.2f | Ask $%.2f\n"
        "  New: Bid $%.2f | Ask $%.2f",
        symbol, stock_id, old_bid, old_ask, bid, ask);
    result->success = true;
    return true;
}

static bool cmd_reset_data(const char* args, admin_result_t* result) {
    // Check for --force flag
    if (args && strstr(args, "--force") != NULL) {
        // For now, we'll just indicate this would reset
        snprintf(result->message, sizeof(result->message),
            "⚠ Database reset not implemented\n"
            "  Please restart server with fresh data files");
        result->success = false;
        return true;
    }
    
    // Ask for confirmation
    snprintf(result->message, sizeof(result->message),
        "⚠ WARNING: This will delete ALL data!\n"
        "  - All user accounts\n"
        "  - All stock definitions\n"
        "  - All portfolios\n"
        "  - All transaction history\n\n"
        "Type 'reset_data --force' to confirm");
    result->success = false;
    result->needs_confirmation = true;
    strncpy(result->confirmation_cmd, "reset_data --force", ADMIN_CMD_MAX_LEN);
    return true;
}

static bool cmd_shutdown(admin_result_t* result) {
    snprintf(result->message, sizeof(result->message),
        "⚠ Shutdown requested\n"
        "  Server will terminate after this message");
    result->success = true;
    // Note: Actual shutdown should be triggered by caller
    return true;
}

bool admin_execute(const char* command, admin_result_t* result) {
    memset(result, 0, sizeof(admin_result_t));
    
    if (!command || strlen(command) == 0) {
        return false;
    }
    
    // Make a mutable copy
    char cmd_copy[ADMIN_CMD_MAX_LEN];
    strncpy(cmd_copy, command, ADMIN_CMD_MAX_LEN - 1);
    cmd_copy[ADMIN_CMD_MAX_LEN - 1] = '\0';
    
    char* cmd = trim(cmd_copy);
    if (strlen(cmd) == 0) {
        return false;
    }
    
    // Extract command name and arguments
    char* space = strchr(cmd, ' ');
    char* args = NULL;
    if (space) {
        *space = '\0';
        args = trim(space + 1);
    }
    
    // Match commands (strict parsing)
    if (strcmp(cmd, "help") == 0) {
        return cmd_help(result);
    } else if (strcmp(cmd, "status") == 0) {
        return cmd_status(result);
    } else if (strcmp(cmd, "create_user") == 0) {
        return cmd_create_user(args ? args : "", result);
    } else if (strcmp(cmd, "credit") == 0) {
        return cmd_credit(args ? args : "", result);
    } else if (strcmp(cmd, "debit") == 0) {
        return cmd_debit(args ? args : "", result);
    } else if (strcmp(cmd, "list_users") == 0) {
        return cmd_list_users(result);
    } else if (strcmp(cmd, "list_stocks") == 0) {
        return cmd_list_stocks(result);
    } else if (strcmp(cmd, "set_price") == 0) {
        return cmd_set_price(args ? args : "", result);
    } else if (strcmp(cmd, "reset_data") == 0) {
        return cmd_reset_data(args, result);
    } else if (strcmp(cmd, "shutdown") == 0) {
        return cmd_shutdown(result);
    } else {
        snprintf(result->message, sizeof(result->message),
            "Invalid command: %s\n\nType 'help' for available commands", cmd);
        result->success = false;
        return true;
    }
}

void admin_get_help(char* buffer, size_t size) {
    snprintf(buffer, size,
        "ADMIN COMMANDS\n"
        "=======================================================\n"
        "\n"
        "USER MANAGEMENT:\n"
        "  create_user <username> <password>\n"
        "  credit <user_id> <amount>\n"
        "  debit <user_id> <amount>\n"
        "  list_users\n"
        "\n"
        "STOCK MANAGEMENT:\n"
        "  set_price <stock_id> <bid> <ask>\n"
        "  list_stocks\n"
        "\n"
        "SERVER CONTROL:\n"
        "  status              Show detailed server stats\n"
        "  reset_data          Reset all data (requires --force)\n"
        "  shutdown            Graceful server shutdown\n"
        "\n"
        "OTHER:\n"
        "  help                Show this help message\n"
        "\n"
        "Press ENTER to return to dashboard");
}
