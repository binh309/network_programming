#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "market.h"
#include "../data/stock_db.h"

// Pseudo-random number generator for price changes
static double random_price_change() {
    // Returns a price change between -2% and +2%
    double change = (rand() % 400 - 200) / 10000.0;  // -0.02 to +0.02
    return change;
}

// Get market update for a stock (with random price fluctuation)
struct market_update get_market_update(uint16_t stock_id) {
    struct market_update update;
    stock_t* stock = stock_db_get_by_id(stock_id);

    if (!stock) {
        memset(&update, 0, sizeof(update));
        return update;
    }

    update.stock_id = stock->stock_id;
    snprintf(update.symbol, sizeof(update.symbol), "%s", stock->symbol);

    // Generate random price change
    double change_percent = random_price_change();
    update.new_price = stock->current_price * (1.0 + change_percent);
    update.price_change_percent = change_percent * 100.0;

    // Update the stock price in database
    stock_db_update_price(stock_id, update.new_price);

    return update;
}

// Market update thread - no longer used (on-demand updates only)
void* market_update_thread(void* arg __attribute__((unused))) {
    printf("[MARKET] Market update thread disabled - using on-demand updates instead\n");
    return NULL;
}
