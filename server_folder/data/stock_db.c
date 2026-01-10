#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stock_db.h"

#define MAX_STOCKS 10
#define DB_PATH "server_folder/data/stocks.txt"

// Global stock database
static stock_t stocks[MAX_STOCKS];
static int stock_count = 0;

// Mock stock names (indexed by stock_id-1)
static const char* stock_names[MAX_STOCKS] = {
    "Apple Inc.",
    "Alphabet Inc.",
    "Microsoft Corp.",
    "Tesla Inc.",
    "Amazon.com Inc.",
    NULL, NULL, NULL, NULL, NULL
};

// Initialize stock database from file
int stock_db_init(const char* db_path) {
    FILE* fp = fopen(db_path ? db_path : DB_PATH, "r");
    if (!fp) {
        fprintf(stderr, "[DB] Cannot open stocks database\n");
        return -1;
    }

    char line[256];
    stock_count = 0;

    while (fgets(line, sizeof(line), fp) && stock_count < MAX_STOCKS) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        // Parse CSV: symbol,price,quantity
        char symbol[16] = {0};
        double price = 0;
        int quantity = 0;

        if (sscanf(line, "%15[^,],%lf,%d", symbol, &price, &quantity) != 3) {
            continue;
        }

        stock_t* s = &stocks[stock_count];
        s->stock_id = stock_count + 1;
        snprintf(s->symbol, sizeof(s->symbol), "%s", symbol);
        snprintf(s->name, sizeof(s->name), "%s", stock_names[stock_count] ? stock_names[stock_count] : symbol);
        s->current_price = price;
        s->available_quantity = quantity;
        s->found = 1;

        printf("[DB] Loaded stock %d: %s (%s) @ $%.2f\n", s->stock_id, s->symbol, s->name, s->current_price);
        stock_count++;
    }

    fclose(fp);
    printf("[DB] Stocks database initialized with %d stocks\n", stock_count);
    return stock_count;
}

// Get all stocks
stock_t* stock_db_get_all(int* count) {
    if (count) {
        *count = stock_count;
    }
    return stocks;
}

// Get stock by ID
stock_t* stock_db_get_by_id(uint16_t stock_id) {
    if (stock_id < 1 || stock_id > stock_count) {
        return NULL;
    }
    return &stocks[stock_id - 1];
}

// Get stock by symbol
stock_t* stock_db_get_by_symbol(const char* symbol) {
    if (!symbol) return NULL;

    for (int i = 0; i < stock_count; i++) {
        if (strcmp(stocks[i].symbol, symbol) == 0) {
            return &stocks[i];
        }
    }
    return NULL;
}

// Update stock price (for market updates)
void stock_db_update_price(uint16_t stock_id, double new_price) {
    if (stock_id < 1 || stock_id > stock_count) {
        return;
    }
    stocks[stock_id - 1].current_price = new_price;
    printf("[DB] Updated stock %s price to $%.2f\n", stocks[stock_id - 1].symbol, new_price);
}

// Update stock quantity (for buy/sell)
void stock_db_update_quantity(uint16_t stock_id, uint32_t new_quantity) {
    if (stock_id < 1 || stock_id > stock_count) {
        return;
    }
    stocks[stock_id - 1].available_quantity = new_quantity;
    printf("[DB] Updated stock %s quantity to %u\n", stocks[stock_id - 1].symbol, new_quantity);
}

// Free function (stocks are global, no malloc)
void stock_db_free(stock_t* stocks __attribute__((unused))) {
    // No-op for static array
}
