#ifndef PORTFOLIO_MANAGER_H
#define PORTFOLIO_MANAGER_H

#include <stdint.h>
#include "../model/portfolio.h"

// Function declarations
int portfolio_mgr_init(void);
portfolio_t* portfolio_mgr_get_or_create(uint32_t user_id);
void portfolio_mgr_cleanup(void);

/**
 * @brief Clear portfolio for a specific user from memory
 * Called on logout to free in-memory portfolio
 */
void portfolio_mgr_clear_user(uint32_t user_id);

/**
 * @brief Reload portfolio for a user from disk
 * Called after transactions (buy/sell) to sync portfolio_manager copy with portfolio_db
 */
void portfolio_mgr_reload_user(uint32_t user_id);

#endif
