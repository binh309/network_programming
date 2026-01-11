#ifndef PORTFOLIO_DB_H
#define PORTFOLIO_DB_H

#include <stdbool.h>
#include "../model/portfolio.h"

// Function declarations
bool portfolio_db_init();
void portfolio_db_destroy();
void portfolio_db_free(portfolio_t* portfolio);

// Thread-safe functions
portfolio_t* portfolio_db_get(uint32_t user_id);
bool portfolio_db_add_holding(uint32_t user_id, uint16_t stock_id, uint32_t quantity, double price);
bool portfolio_db_remove_holding(uint32_t user_id, uint16_t stock_id, uint32_t quantity);
bool portfolio_db_persist();
bool portfolio_db_load();

#endif
