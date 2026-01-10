#ifndef MARKET_H
#define MARKET_H

#include <time.h>
#include "../data/stock_db.h"
#include "../network/protocol.h"

// Market updater thread
void* market_update_thread(void* arg);

// Get price change for a stock
struct market_update get_market_update(uint16_t stock_id);

#endif
