#include "../ui/tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "portfolio_manager.h"
#include "../model/portfolio.h"
#include "../data/portfolio_db.h"

// Increased to support test accounts (IDs 9001-9100)
#define MAX_USERS 10000

static portfolio_t* portfolios[MAX_USERS];
static pthread_mutex_t portfolio_lock = PTHREAD_MUTEX_INITIALIZER;

// Initialize portfolio manager
int portfolio_mgr_init(void) {
    pthread_mutex_lock(&portfolio_lock);
    for (int i = 0; i < MAX_USERS; i++) {
        portfolios[i] = NULL;
    }
    pthread_mutex_unlock(&portfolio_lock);
    server_debug("[PORTFOLIO_MGR] Initialized\n");
    return 0;
}

// Get or create portfolio for user
portfolio_t* portfolio_mgr_get_or_create(uint32_t user_id) {
    if (user_id < 1 || user_id > MAX_USERS) {
        server_debug("[PORTFOLIO_MGR] Invalid user_id: %u\n", user_id);
        return NULL;
    }

    pthread_mutex_lock(&portfolio_lock);

    // Check if portfolio exists in memory
    if (portfolios[user_id - 1] == NULL) {
        // Create empty portfolio
        portfolios[user_id - 1] = portfolio_create(user_id);
        server_debug("[PORTFOLIO_MGR] Created portfolio for user %u\n", user_id);
        
        // Load from disk if it exists (via portfolio_db_get which loads from file)
        // This restores any previous holdings
        extern portfolio_t* portfolio_db_get(uint32_t user_id);
        portfolio_t* disk_copy = portfolio_db_get(user_id);
        if (disk_copy && disk_copy->holding_count > 0) {
            server_debug("[PORTFOLIO_MGR] Loading %u holdings from disk for user %u\n", 
                   disk_copy->holding_count, user_id);
            // Copy holdings from disk into persistent manager portfolio
            portfolios[user_id - 1]->holding_count = disk_copy->holding_count;
            portfolios[user_id - 1]->capacity = disk_copy->capacity;
            free(portfolios[user_id - 1]->holdings);
            portfolios[user_id - 1]->holdings = malloc(sizeof(holding_t) * disk_copy->holding_count);
            memcpy(portfolios[user_id - 1]->holdings, disk_copy->holdings, 
                   sizeof(holding_t) * disk_copy->holding_count);
            server_debug("[PORTFOLIO_MGR] Restored %u holdings for user %u\n", 
                   disk_copy->holding_count, user_id);
        }
        if (disk_copy) portfolio_db_free(disk_copy);
    }

    portfolio_t* portfolio = portfolios[user_id - 1];
    pthread_mutex_unlock(&portfolio_lock);

    return portfolio;
}

// Cleanup (called on shutdown)
void portfolio_mgr_cleanup(void) {
    pthread_mutex_lock(&portfolio_lock);
    for (int i = 0; i < MAX_USERS; i++) {
        if (portfolios[i] != NULL) {
            // Free the portfolio to prevent memory leak
            portfolio_free(portfolios[i]);
            portfolios[i] = NULL;
        }
    }
    pthread_mutex_unlock(&portfolio_lock);
    server_debug("[PORTFOLIO_MGR] Cleaned up\n");
}

// Clear portfolio for a user (called on logout)
void portfolio_mgr_clear_user(uint32_t user_id) {
    if (user_id < 1 || user_id > MAX_USERS) {
        server_debug("[PORTFOLIO_MGR] Cannot clear invalid user_id: %u\n", user_id);
        return;
    }

    pthread_mutex_lock(&portfolio_lock);
    if (portfolios[user_id - 1] != NULL) {
        server_debug("[PORTFOLIO_MGR] Clearing portfolio for user %u from memory\n", user_id);
        // Free the portfolio to prevent memory leak (data is persisted to disk)
        portfolio_free(portfolios[user_id - 1]);
        portfolios[user_id - 1] = NULL;
    }
    pthread_mutex_unlock(&portfolio_lock);
}

// Reload portfolio for a user from disk
// This syncs the in-memory copy with the disk file after transactions
void portfolio_mgr_reload_user(uint32_t user_id) {
    if (user_id < 1 || user_id > MAX_USERS) {
        server_debug("[PORTFOLIO_MGR] Cannot reload invalid user_id: %u\n", user_id);
        return;
    }

    pthread_mutex_lock(&portfolio_lock);
    
    // Load fresh copy from disk
    portfolio_t* disk_copy = portfolio_db_get(user_id);
    
    if (disk_copy) {
        // Free old in-memory copy if it exists
        if (portfolios[user_id - 1] != NULL) {
            portfolio_free(portfolios[user_id - 1]);
        }
        
        // Replace with fresh disk copy
        portfolios[user_id - 1] = disk_copy;
        server_debug("[PORTFOLIO_MGR] Reloaded portfolio for user %u from disk (%u holdings)\n", 
               user_id, disk_copy->holding_count);
    } else {
        // If no portfolio exists on disk, clear memory copy
        if (portfolios[user_id - 1] != NULL) {
            portfolio_free(portfolios[user_id - 1]);
            portfolios[user_id - 1] = NULL;
        }
    }
    
    pthread_mutex_unlock(&portfolio_lock);
}

