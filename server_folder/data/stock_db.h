#ifndef STOCK_DB_H
#define STOCK_DB_H

#include <stdint.h>
#include <stdbool.h>

// Stock record structure
typedef struct {
    uint16_t stock_id;
    char symbol[16];
    char name[32];
    double best_bid;
    double best_ask;
    double last_price;
    uint32_t volume;
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

#endif
