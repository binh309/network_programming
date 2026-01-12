#include "ui/tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "portfolio_db.h"
#include "account_db.h"  // For TEST_ACCOUNT_ID_START/END
#include "../model/error.h"

#define DB_PATH "data/portfolios.txt"

// Hash table for sparse user IDs (supports test accounts at 9001+)
typedef struct portfolio_node {
    portfolio_t* portfolio;
    struct portfolio_node* next;
} portfolio_node_t;

#define HASH_BUCKETS 1024
static portfolio_node_t* portfolio_hash[HASH_BUCKETS] = {NULL};
static pthread_mutex_t db_mutex;

// Hash function
static inline uint32_t hash_user_id(uint32_t user_id) {
    return user_id % HASH_BUCKETS;
}

// Forward declarations
static portfolio_t* find_or_create_portfolio(uint32_t user_id);
static portfolio_t* find_portfolio(uint32_t user_id);

// Initialize portfolio database
bool portfolio_db_init() {
    if (pthread_mutex_init(&db_mutex, NULL) != 0) {
        fprintf(stderr, "[PORTF_DB] Mutex init failed\n");
        return false;
    }
    
    // Initialize hash table
    for (int i = 0; i < HASH_BUCKETS; i++) {
        portfolio_hash[i] = NULL;
    }
    
    if (!portfolio_db_load()) {
        fprintf(stderr, "[PORTF_DB] Failed to load portfolio data. Starting fresh.\n");
    }
    server_debug("[PORTF_DB] Portfolio database initialized\n");
    return true;
}

// Destroy portfolio database (free memory)
void portfolio_db_destroy() {
    pthread_mutex_lock(&db_mutex);
    for (int i = 0; i < HASH_BUCKETS; i++) {
        portfolio_node_t* node = portfolio_hash[i];
        while (node) {
            portfolio_node_t* next = node->next;
            if (node->portfolio) {
                free(node->portfolio->holdings);
                free(node->portfolio);
            }
            free(node);
            node = next;
        }
        portfolio_hash[i] = NULL;
    }
    pthread_mutex_unlock(&db_mutex);
    pthread_mutex_destroy(&db_mutex);
    server_debug("[PORTF_DB] Portfolio database destroyed\n");
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
    if (user_id == 0) {
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

    portfolio_t* portfolio = find_portfolio(user_id);
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

// Load portfolios from disk (called without lock during init)
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
        p = strchr(p, ':');
        if (!p) continue;
        p++;  // Skip the ':'
        
        portfolio_t* portfolio = find_or_create_portfolio(user_id);
        if (!portfolio) continue;
        
        uint16_t stock_id;
        uint32_t qty;
        double price;

        while (sscanf(p, " %hu,%u,%lf%n", &stock_id, &qty, &price, &items_read) == 3) {
            // Add holding directly (we're in init, avoid recursive locking)
            holding_t* holding = NULL;
            for (uint32_t i = 0; i < portfolio->holding_count; i++) {
                if (portfolio->holdings[i].stock_id == stock_id) {
                    holding = &portfolio->holdings[i];
                    break;
                }
            }
            
            if (holding) {
                double total_value_before = holding->quantity * holding->average_purchase_price;
                double total_value_new = qty * price;
                holding->quantity += qty;
                holding->average_purchase_price = (total_value_before + total_value_new) / holding->quantity;
            } else {
                if (portfolio->holding_count >= portfolio->capacity) {
                    portfolio->capacity = portfolio->capacity > 0 ? portfolio->capacity * 2 : 10;
                    holding_t* new_holdings = realloc(portfolio->holdings, sizeof(holding_t) * portfolio->capacity);
                    if (new_holdings) {
                        portfolio->holdings = new_holdings;
                    }
                }
                if (portfolio->holding_count < portfolio->capacity) {
                    holding_t* new_holding = &portfolio->holdings[portfolio->holding_count];
                    new_holding->stock_id = stock_id;
                    new_holding->quantity = qty;
                    new_holding->average_purchase_price = price;
                    portfolio->holding_count++;
                }
            }
            p += items_read;
        }
    }

    fclose(fp);
    return true;
}

// Persist all portfolios to disk (must be called with mutex held)
bool portfolio_db_persist() {
    FILE* fp = fopen(DB_PATH, "w");
    if (!fp) {
        fprintf(stderr, "[PORTF_DB] Failed to open portfolio file for writing\n");
        return false;
    }

    for (int i = 0; i < HASH_BUCKETS; i++) {
        portfolio_node_t* node = portfolio_hash[i];
        while (node) {
            portfolio_t* p = node->portfolio;
            if (p && p->holding_count > 0) {
                fprintf(fp, "%u:", p->user_id);
                for (uint32_t j = 0; j < p->holding_count; j++) {
                    holding_t* h = &p->holdings[j];
                    fprintf(fp, " %hu,%u,%.2f", h->stock_id, h->quantity, h->average_purchase_price);
                }
                fprintf(fp, "\n");
            }
            node = node->next;
        }
    }

    fclose(fp);
    return true;
}

// Delete all test account portfolios (IDs 9001-9100)
int portfolio_db_delete_test_portfolios(void) {
    int deleted = 0;
    pthread_mutex_lock(&db_mutex);
    
    for (int i = 0; i < HASH_BUCKETS; i++) {
        portfolio_node_t** pp = &portfolio_hash[i];
        while (*pp) {
            portfolio_t* portfolio = (*pp)->portfolio;
            if (portfolio && 
                portfolio->user_id >= TEST_ACCOUNT_ID_START && 
                portfolio->user_id <= TEST_ACCOUNT_ID_END) {
                // Remove this node
                portfolio_node_t* to_delete = *pp;
                *pp = (*pp)->next;
                free(portfolio->holdings);
                free(portfolio);
                free(to_delete);
                deleted++;
            } else {
                pp = &(*pp)->next;
            }
        }
    }
    
    portfolio_db_persist();
    pthread_mutex_unlock(&db_mutex);
    return deleted;
}

// --- Internal Helper Functions ---

// Find a portfolio for a user (does not create)
// NOTE: Must be called with mutex held
static portfolio_t* find_portfolio(uint32_t user_id) {
    uint32_t bucket = hash_user_id(user_id);
    portfolio_node_t* node = portfolio_hash[bucket];
    
    while (node) {
        if (node->portfolio && node->portfolio->user_id == user_id) {
            return node->portfolio;
        }
        node = node->next;
    }
    return NULL;
}

// Find a portfolio for a user; if it doesn't exist, create it.
// NOTE: Must be called with mutex held
static portfolio_t* find_or_create_portfolio(uint32_t user_id) {
    if (user_id == 0) return NULL;
    
    // First try to find existing
    portfolio_t* existing = find_portfolio(user_id);
    if (existing) return existing;
    
    // Create new portfolio
    portfolio_t* portfolio = malloc(sizeof(portfolio_t));
    if (!portfolio) return NULL;
    
    portfolio->user_id = user_id;
    portfolio->holding_count = 0;
    portfolio->capacity = 10;
    portfolio->holdings = malloc(sizeof(holding_t) * portfolio->capacity);
    
    if (!portfolio->holdings) {
        free(portfolio);
        return NULL;
    }
    
    // Create node and insert into hash table
    portfolio_node_t* node = malloc(sizeof(portfolio_node_t));
    if (!node) {
        free(portfolio->holdings);
        free(portfolio);
        return NULL;
    }
    
    node->portfolio = portfolio;
    uint32_t bucket = hash_user_id(user_id);
    node->next = portfolio_hash[bucket];
    portfolio_hash[bucket] = node;
    
    return portfolio;
}
