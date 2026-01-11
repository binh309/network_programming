#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include "market.h"
#include "../data/stock_db.h"

#define MARKET_UPDATE_INTERVAL_S 30 // seconds

// Returns a random float between -1 and 1
static double random_walk() {
    return ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0;
}

// Update the price of a single stock
static void update_stock_price(stock_t* stock) {
    if (!stock) return;

    // Simulate a slight random walk for the last price
    double volatility = 0.015; // e.g., 1.5% volatility
    double change_percent = volatility * random_walk();
    double new_last_price = stock->last_price * (1.0 + change_percent);

    // Ensure price doesn't go negative
    if (new_last_price < 0.01) {
        new_last_price = 0.01;
    }

    // The spread is the difference between bid and ask
    double spread = new_last_price * 0.005; // e.g., 0.5% spread
    if (spread < 0.01) {
        spread = 0.01;
    }

    double new_bid = new_last_price - (spread / 2.0);
    double new_ask = new_last_price + (spread / 2.0);

    // Update the stock in the database
    if (stock_db_update_price(stock->stock_id, new_bid, new_ask, new_last_price)) {
        printf("[MARKET] Updated %s: Bid: %.2f, Ask: %.2f, Last: %.2f\n",
               stock->symbol, new_bid, new_ask, new_last_price);
    }
}

// Market update thread
void* market_update_thread(void* arg __attribute__((unused))) {
    printf("[MARKET] Market simulation thread started.\n");
    srand(time(NULL));

    while (1) {
        int stock_count = 0;
        stock_t* all_stocks = stock_db_get_all(&stock_count);

        if (all_stocks && stock_count > 0) {
            for (int i = 0; i < stock_count; i++) {
                update_stock_price(&all_stocks[i]);
            }
            stock_db_free(all_stocks); // Free the copied array
        }
        
        sleep(MARKET_UPDATE_INTERVAL_S);
    }

    return NULL;
}