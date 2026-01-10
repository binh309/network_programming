#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../model/portfolio.h"

#define INITIAL_HOLDINGS_CAPACITY 10

// Create a new portfolio for a user
portfolio_t* portfolio_create(uint32_t user_id) {
    portfolio_t* portfolio = malloc(sizeof(portfolio_t));
    if (!portfolio) return NULL;
    
    portfolio->user_id = user_id;
    portfolio->holding_count = 0;
    portfolio->capacity = INITIAL_HOLDINGS_CAPACITY;
    portfolio->holdings = malloc(sizeof(holding_t) * portfolio->capacity);
    
    if (!portfolio->holdings) {
        free(portfolio);
        return NULL;
    }
    
    return portfolio;
}

// Free a portfolio
void portfolio_free(portfolio_t* portfolio) {
    if (!portfolio) return;
    if (portfolio->holdings) {
        free(portfolio->holdings);
    }
    free(portfolio);
}

// Find a holding by stock_id
holding_t* portfolio_find_holding(portfolio_t* portfolio, uint16_t stock_id) {
    if (!portfolio || !portfolio->holdings) return NULL;
    
    for (uint32_t i = 0; i < portfolio->holding_count; i++) {
        if (portfolio->holdings[i].stock_id == stock_id) {
            return &portfolio->holdings[i];
        }
    }
    return NULL;
}

// Add or increase a holding
int portfolio_add_holding(portfolio_t* portfolio, uint16_t stock_id, uint32_t quantity, double price) {
    if (!portfolio || !portfolio->holdings) return -1;
    if (quantity == 0) return 0;
    
    // Find existing holding
    holding_t* holding = portfolio_find_holding(portfolio, stock_id);
    
    if (holding) {
        // Update existing holding with weighted average price
        double total_before = holding->quantity * holding->average_purchase_price;
        double total_new = quantity * price;
        holding->quantity += quantity;
        holding->average_purchase_price = (total_before + total_new) / holding->quantity;
        return 0;
    }
    
    // Add new holding
    if (portfolio->holding_count >= portfolio->capacity) {
        // Expand capacity
        portfolio->capacity *= 2;
        holding_t* new_holdings = realloc(portfolio->holdings, sizeof(holding_t) * portfolio->capacity);
        if (!new_holdings) return -1;
        portfolio->holdings = new_holdings;
    }
    
    holding_t* new_holding = &portfolio->holdings[portfolio->holding_count];
    new_holding->stock_id = stock_id;
    new_holding->quantity = quantity;
    new_holding->average_purchase_price = price;
    portfolio->holding_count++;
    
    return 0;
}

// Remove or decrease a holding
int portfolio_remove_holding(portfolio_t* portfolio, uint16_t stock_id, uint32_t quantity) {
    if (!portfolio || !portfolio->holdings) return -1;
    if (quantity == 0) return 0;
    
    holding_t* holding = portfolio_find_holding(portfolio, stock_id);
    if (!holding) return -1;  // Don't own this stock
    
    if (holding->quantity < quantity) {
        return -2;  // Insufficient quantity
    }
    
    holding->quantity -= quantity;
    
    // If quantity becomes 0, remove the holding
    if (holding->quantity == 0) {
        // Find the index
        uint32_t index = holding - portfolio->holdings;
        // Shift remaining holdings
        for (uint32_t i = index; i < portfolio->holding_count - 1; i++) {
            portfolio->holdings[i] = portfolio->holdings[i + 1];
        }
        portfolio->holding_count--;
    }
    
    return 0;
}

// Print portfolio (for debugging)
void portfolio_print(portfolio_t* portfolio) {
    if (!portfolio) return;
    
    printf("[PORTFOLIO] User ID: %u, Holdings: %u\n", portfolio->user_id, portfolio->holding_count);
    for (uint32_t i = 0; i < portfolio->holding_count; i++) {
        holding_t* h = &portfolio->holdings[i];
        printf("  [%u] Stock ID: %u, Qty: %u, Avg Price: $%.2f\n",
               i, h->stock_id, h->quantity, h->average_purchase_price);
    }
}
