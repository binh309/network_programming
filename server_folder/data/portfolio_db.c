#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "portfolio_db.h"
#include "../model/error.h"

#define MAX_USERS 100
#define DB_PATH "data/portfolios.txt"

// In-memory database
static portfolio_t* user_portfolios[MAX_USERS] = {NULL};
static pthread_mutex_t db_mutex;

// Forward declarations for internal functions
static portfolio_t* find_or_create_portfolio(uint32_t user_id);

// Initialize portfolio database
bool portfolio_db_init() {
    if (pthread_mutex_init(&db_mutex, NULL) != 0) {
        fprintf(stderr, "[PORTF_DB] Mutex init failed\n");
        return false;
    }
    if (!portfolio_db_load()) {
        fprintf(stderr, "[PORTF_DB] Failed to load portfolio data. Starting fresh.\n");
    }
    printf("[PORTF_DB] Portfolio database initialized\n");
    return true;
}

// Destroy portfolio database (free memory)
void portfolio_db_destroy() {
    pthread_mutex_lock(&db_mutex);
    for (int i = 0; i < MAX_USERS; i++) {
        if (user_portfolios[i]) {
            free(user_portfolios[i]->holdings);
            free(user_portfolios[i]);
            user_portfolios[i] = NULL;
        }
    }
    pthread_mutex_unlock(&db_mutex);
    pthread_mutex_destroy(&db_mutex);
    printf("[PORTF_DB] Portfolio database destroyed\n");
}

// Free a heap-allocated portfolio copy
void portfolio_db_free(portfolio_t* portfolio) {
    if (portfolio) {
        free(portfolio->holdings);
        free(portfolio);
    }
}

// Get a user's portfolio (returns a heap-allocated copy)
portfolio_t* portfolio_db_get(uint32_t user_id) {
    if (user_id == 0 || user_id > MAX_USERS) {
        return NULL;
    }

    pthread_mutex_lock(&db_mutex);
    
    portfolio_t* original = find_or_create_portfolio(user_id);
    if (!original) {
        pthread_mutex_unlock(&db_mutex);
        return NULL;
    }

    // Create a deep copy to return to the caller
    portfolio_t* copy = malloc(sizeof(portfolio_t));
    if (!copy) {
        pthread_mutex_unlock(&db_mutex);
        return NULL;
    }

    copy->user_id = original->user_id;
    copy->holding_count = original->holding_count;
    copy->capacity = original->holding_count > 0 ? original->holding_count : 1;
    copy->holdings = malloc(sizeof(holding_t) * copy->capacity);
    
    if (!copy->holdings) {
        free(copy);
        pthread_mutex_unlock(&db_mutex);
        return NULL;
    }

    if (original->holding_count > 0) {
        memcpy(copy->holdings, original->holdings, sizeof(holding_t) * original->holding_count);
    }

    pthread_mutex_unlock(&db_mutex);
    return copy;
}

// Add a holding to a user's portfolio
bool portfolio_db_add_holding(uint32_t user_id, uint16_t stock_id, uint32_t quantity, double price) {
    if (user_id == 0 || quantity == 0) {
        return false;
    }

    pthread_mutex_lock(&db_mutex);

    portfolio_t* portfolio = find_or_create_portfolio(user_id);
    if (!portfolio) {
        pthread_mutex_unlock(&db_mutex);
        return false;
    }
    
    holding_t* holding = NULL;
    for (uint32_t i = 0; i < portfolio->holding_count; i++) {
        if (portfolio->holdings[i].stock_id == stock_id) {
            holding = &portfolio->holdings[i];
            break;
        }
    }

    if (holding) {
        double total_value_before = holding->quantity * holding->average_purchase_price;
        double total_value_new = quantity * price;
        holding->quantity += quantity;
        holding->average_purchase_price = (total_value_before + total_value_new) / holding->quantity;
    } else {
        if (portfolio->holding_count >= portfolio->capacity) {
            portfolio->capacity = portfolio->capacity > 0 ? portfolio->capacity * 2 : 10;
            holding_t* new_holdings = realloc(portfolio->holdings, sizeof(holding_t) * portfolio->capacity);
            if (!new_holdings) {
                pthread_mutex_unlock(&db_mutex);
                return false;
            }
            portfolio->holdings = new_holdings;
        }
        holding_t* new_holding = &portfolio->holdings[portfolio->holding_count];
        new_holding->stock_id = stock_id;
        new_holding->quantity = quantity;
        new_holding->average_purchase_price = price;
        portfolio->holding_count++;
    }

    bool success = portfolio_db_persist();
    pthread_mutex_unlock(&db_mutex);
    return success;
}

// Remove a holding from a user's portfolio
bool portfolio_db_remove_holding(uint32_t user_id, uint16_t stock_id, uint32_t quantity) {
    if (user_id == 0 || quantity == 0) {
        return false;
    }

    pthread_mutex_lock(&db_mutex);

    portfolio_t* portfolio = find_or_create_portfolio(user_id);
    if (!portfolio) {
        pthread_mutex_unlock(&db_mutex);
        return false;
    }

    int holding_idx = -1;
    for (uint32_t i = 0; i < portfolio->holding_count; i++) {
        if (portfolio->holdings[i].stock_id == stock_id) {
            holding_idx = i;
            break;
        }
    }

    if (holding_idx == -1 || portfolio->holdings[holding_idx].quantity < quantity) {
        pthread_mutex_unlock(&db_mutex);
        return false; // User does not own enough of the stock
    }

    portfolio->holdings[holding_idx].quantity -= quantity;

    if (portfolio->holdings[holding_idx].quantity == 0) {
        for (uint32_t i = holding_idx; i < portfolio->holding_count - 1; i++) {
            portfolio->holdings[i] = portfolio->holdings[i + 1];
        }
        portfolio->holding_count--;
    }

    bool success = portfolio_db_persist();
    pthread_mutex_unlock(&db_mutex);
    return success;
}

// Load portfolios from disk
bool portfolio_db_load() {
    FILE* fp = fopen(DB_PATH, "r");
    if (!fp) {
        // File might not exist on first run, which is not an error
        return true;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        uint32_t user_id;
        int items_read = 0;
        const char* p = line;

        if (sscanf(p, "%u:", &user_id) != 1) continue;
        p = strchr(p, ':') + 1;
        if (!p) continue;
        
        portfolio_t* portfolio = find_or_create_portfolio(user_id);
        if (!portfolio) continue;
        
        uint16_t stock_id;
        uint32_t qty;
        double price;

        while (sscanf(p, " %hu,%u,%lf%n", &stock_id, &qty, &price, &items_read) == 3) {
            portfolio_db_add_holding(user_id, stock_id, qty, price);
            p += items_read;
        }
    }

    fclose(fp);
    return true;
}

// Persist all portfolios to disk
bool portfolio_db_persist() {
    FILE* fp = fopen(DB_PATH, "w");
    if (!fp) {
        fprintf(stderr, "[PORTF_DB] Failed to open portfolio file for writing\n");
        return false;
    }

    for (int i = 0; i < MAX_USERS; i++) {
        portfolio_t* p = user_portfolios[i];
        if (p && p->holding_count > 0) {
            fprintf(fp, "%u:", p->user_id);
            for (uint32_t j = 0; j < p->holding_count; j++) {
                holding_t* h = &p->holdings[j];
                fprintf(fp, " %hu,%u,%.2f", h->stock_id, h->quantity, h->average_purchase_price);
            }
            fprintf(fp, "\n");
        }
    }

    fclose(fp);
    return true;
}

// --- Internal Helper Functions ---

// Find a portfolio for a user; if it doesn't exist, create it.
// NOTE: This function MUST be called within a locked mutex.
static portfolio_t* find_or_create_portfolio(uint32_t user_id) {
    if (user_id == 0 || user_id > MAX_USERS) return NULL; 
    
    int user_idx = user_id - 1;

    if (!user_portfolios[user_idx]) {
        user_portfolios[user_idx] = malloc(sizeof(portfolio_t));
        if (!user_portfolios[user_idx]) return NULL;

        user_portfolios[user_idx]->user_id = user_id;
        user_portfolios[user_idx]->holding_count = 0;
        user_portfolios[user_idx]->capacity = 10; // Initial capacity
        user_portfolios[user_idx]->holdings = malloc(sizeof(holding_t) * 10);
        
        if (!user_portfolios[user_idx]->holdings) {
            free(user_portfolios[user_idx]);
            user_portfolios[user_idx] = NULL;
            return NULL;
        }
    }
    return user_portfolios[user_idx];
}
