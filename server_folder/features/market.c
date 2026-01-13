#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include "market.h"
#include "../data/stock_db.h"
#include "../ui/tui.h"

#define MARKET_UPDATE_INTERVAL_S 30 // seconds

// Global flag to gracefully shutdown market thread
static volatile sig_atomic_t market_running = 1;

// Returns a random float between -1 and 1
static double random_walk() {
    return ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0;
}

// Update the price of a single stock
static void update_stock_price(stock_t* stock) {
    if (!stock) return;

    // Simulate a slight random walk for the last price
    // 0.2% volatility is more realistic (1.5% was too high - would be 50%+ daily)
    double volatility = 0.002;
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
        // Only log to TUI, don't use printf to avoid breaking ncurses
        tui_log(LOG_INFO, "[MARKET] %s: Bid:%.2f Ask:%.2f", stock->symbol, new_bid, new_ask);
    }
}

// Function to gracefully shutdown market thread
void market_stop(void) {
    market_running = 0;
    // Don't log here - TUI may already be shut down
}

// Market update thread
void* market_update_thread(void* arg __attribute__((unused))) {
    tui_log(LOG_INFO, "Market simulation thread started");
    srand(time(NULL));

    while (market_running) {
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

    tui_log(LOG_INFO, "Market simulation thread shutting down");
    return NULL;
}