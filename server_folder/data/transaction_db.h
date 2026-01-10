#ifndef TRANSACTION_DB_H
#define TRANSACTION_DB_H

#include <stdint.h>
#include <time.h>

// Transaction types
#define TRANSACTION_BUY  1
#define TRANSACTION_SELL 2

// Transaction record
typedef struct {
    uint32_t order_id;
    uint32_t user_id;
    uint8_t type;  // BUY or SELL
    uint16_t stock_id;
    uint32_t quantity;
    double price;
    double total_amount;  // total_cost for buy, proceeds for sell
    time_t timestamp;
} transaction_t;

// Function declarations
int transaction_db_init(void);
uint32_t transaction_db_record(uint32_t user_id, uint8_t type, uint16_t stock_id, 
                               uint32_t quantity, double price);
void transaction_db_print_all(void);
void transaction_db_print_user(uint32_t user_id);

#endif
