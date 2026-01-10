#ifndef PORTFOLIO_H
#define PORTFOLIO_H

#include <stdint.h>

// Holding structure - represents shares owned by a user
typedef struct {
    uint16_t stock_id;
    uint32_t quantity;
    double average_purchase_price;
} holding_t;

// Portfolio structure - all holdings for a user
typedef struct {
    uint32_t user_id;
    holding_t* holdings;
    uint32_t holding_count;
    uint32_t capacity;
} portfolio_t;

// Function declarations
portfolio_t* portfolio_create(uint32_t user_id);
void portfolio_free(portfolio_t* portfolio);
int portfolio_add_holding(portfolio_t* portfolio, uint16_t stock_id, uint32_t quantity, double price);
int portfolio_remove_holding(portfolio_t* portfolio, uint16_t stock_id, uint32_t quantity);
holding_t* portfolio_find_holding(portfolio_t* portfolio, uint16_t stock_id);
void portfolio_print(portfolio_t* portfolio);

#endif
