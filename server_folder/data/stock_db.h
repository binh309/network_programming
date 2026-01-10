#ifndef STOCK_DB_H
#define STOCK_DB_H

#include <stdint.h>

// Stock record structure
typedef struct {
    uint16_t stock_id;
    char symbol[16];
    char name[32];
    double current_price;
    uint32_t available_quantity;
    int found;
} stock_t;

// Function declarations
stock_t* stock_db_get_all(int* count);
stock_t* stock_db_get_by_id(uint16_t stock_id);
stock_t* stock_db_get_by_symbol(const char* symbol);
int stock_db_init(const char* db_path);
void stock_db_free(stock_t* stocks);
void stock_db_update_price(uint16_t stock_id, double new_price);
void stock_db_update_quantity(uint16_t stock_id, uint32_t new_quantity);

#endif
