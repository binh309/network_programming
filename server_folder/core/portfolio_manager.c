#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "portfolio_manager.h"
#include "../model/portfolio.h"

#define MAX_USERS 100

static portfolio_t* portfolios[MAX_USERS];
static pthread_mutex_t portfolio_lock = PTHREAD_MUTEX_INITIALIZER;

// Initialize portfolio manager
int portfolio_mgr_init(void) {
    pthread_mutex_lock(&portfolio_lock);
    for (int i = 0; i < MAX_USERS; i++) {
        portfolios[i] = NULL;
    }
    pthread_mutex_unlock(&portfolio_lock);
    printf("[PORTFOLIO_MGR] Initialized\n");
    return 0;
}

// Get or create portfolio for user
portfolio_t* portfolio_mgr_get_or_create(uint32_t user_id) {
    if (user_id < 1 || user_id > MAX_USERS) {
        printf("[PORTFOLIO_MGR] Invalid user_id: %u\n", user_id);
        return NULL;
    }

    pthread_mutex_lock(&portfolio_lock);

    // Check if portfolio exists
    if (portfolios[user_id - 1] == NULL) {
        portfolios[user_id - 1] = portfolio_create(user_id);
        printf("[PORTFOLIO_MGR] Created portfolio for user %u\n", user_id);
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
            // Don't free since we manage lifetime
            // portfolio_free(portfolios[i]);
            portfolios[i] = NULL;
        }
    }
    pthread_mutex_unlock(&portfolio_lock);
    printf("[PORTFOLIO_MGR] Cleaned up\n");
}
