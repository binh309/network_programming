#ifndef STOCK_DB_H
#define STOCK_DB_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Stock record structure
typedef struct {
    uint16_t stock_id;
    char symbol[16];
    char name[32];
    double best_bid;
    uint32_t bid_quantity;          // Quantity available at bid price
    double best_ask;
    uint32_t ask_quantity;          // Quantity available at ask price
    double last_price;
    uint32_t last_quantity;         // Quantity traded at last price
    uint32_t volume;
    time_t last_update_time;        // When price was last updated (Unix timestamp)
} stock_t;

// Function declarations
bool stock_db_init(const char* db_path);
void stock_db_free(stock_t* stocks);

// Thread-safe functions
stock_t* stock_db_get_all(int* count);
stock_t* stock_db_get_by_id(uint16_t stock_id);
stock_t* stock_db_get_by_symbol(const char* symbol);

double stock_db_get_price(uint16_t stock_id);
bool stock_db_update_price(uint16_t stock_id, double new_bid, double new_ask, double new_last);
bool stock_db_update_volume(uint16_t stock_id, uint32_t new_volume);

// ========== TOCTOU FIX: Atomic check-and-update functions ==========
// These functions lock, check availability, and update in one atomic operation
// Prevents race condition where two threads both read, then both update

// Atomic buy: Check volume >= quantity, then deduct. Returns true if successful.
bool stock_db_atomic_buy(uint16_t stock_id, uint32_t quantity, uint32_t* new_volume);

// Atomic sell: Add quantity back to volume. Returns true if successful.
bool stock_db_atomic_sell(uint16_t stock_id, uint32_t quantity, uint32_t* new_volume);

// Reset all stock volumes to a given value (for load testing)
bool stock_db_reset_volumes(uint32_t volume);

#endif
