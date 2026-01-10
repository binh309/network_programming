#ifndef PORTFOLIO_MANAGER_H
#define PORTFOLIO_MANAGER_H

#include <stdint.h>
#include "../model/portfolio.h"

// Function declarations
int portfolio_mgr_init(void);
portfolio_t* portfolio_mgr_get_or_create(uint32_t user_id);
void portfolio_mgr_cleanup(void);

#endif
