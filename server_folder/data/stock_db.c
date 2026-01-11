#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "stock_db.h"

#define MAX_STOCKS 50
#define DB_PATH "data/stocks.txt"

// In-memory database
static stock_t stocks[MAX_STOCKS];
static int stock_count = 0;
static pthread_mutex_t db_mutex;

// Mock stock names (indexed by stock_id-1)
static const char* stock_names[] = {
    "Apple Inc.", "Microsoft Corp.", "Amazon.com Inc.", "NVIDIA Corp.",
    "Alphabet Inc.", "Tesla Inc.", "Meta Platforms Inc.", "Berkshire Hathaway",
    "Eli Lilly and Co.", "Taiwan Semiconductor"
};
#define NUM_STOCK_NAMES (sizeof(stock_names) / sizeof(stock_names[0]))

static bool persist_db() {
    FILE* fp = fopen(DB_PATH, "w");
    if (!fp) {
        fprintf(stderr, "[DB] Cannot open stocks file for writing: %s\n", DB_PATH);
        return false;
    }

    for (int i = 0; i < stock_count; i++) {
        fprintf(fp, "%s,%.2f,%.2f,%.2f,%u\n",
                stocks[i].symbol,
                stocks[i].best_bid,
                stocks[i].best_ask,
                stocks[i].last_price,
                stocks[i].volume);
    }

    fclose(fp);
    return true;
}

// Initialize stock database from file
bool stock_db_init(const char* db_path) {
    if (pthread_mutex_init(&db_mutex, NULL) != 0) {
        fprintf(stderr, "[DB] Mutex init failed\n");
        return false;
    }
    
    const char* path = db_path ? db_path : DB_PATH;
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "[DB] Cannot open stocks database: %s\n", path);
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) && stock_count < MAX_STOCKS) {
        stock_t* s = &stocks[stock_count];
        s->stock_id = stock_count + 1;

        // sscanf for comma-separated values
        int result = sscanf(line, "%15[^,],%lf,%lf,%lf,%u",
               s->symbol, &s->best_bid, &s->best_ask, &s->last_price, &s->volume);

        if (result != 5) {
            // Fallback: Try parsing simpler format (SYMBOL,PRICE,VOLUME)
            double price;
            if (sscanf(line, "%15[^,],%lf,%u", s->symbol, &price, &s->volume) == 3) {
                s->best_bid = price;
                s->best_ask = price;
                s->last_price = price;
            } else {
                fprintf(stderr, "[DB] Malformed line in stocks.txt: %s", line);
                continue;
            }
        }

        // Assign a name from the mock list
        if ((size_t)stock_count < NUM_STOCK_NAMES) {
            strncpy(s->name, stock_names[stock_count], sizeof(s->name) - 1);
        } else {
            strncpy(s->name, "Unknown Co.", sizeof(s->name) - 1);
        }
        s->name[sizeof(s->name) - 1] = '\0';
        
        printf("[DB] Loaded stock #%d: %s (%s) - Last: $%.2f\n", s->stock_id, s->symbol, s->name, s->last_price);
        stock_count++;
    }

    fclose(fp);
    printf("[DB] Stocks database initialized with %d stocks\n", stock_count);
    return true;
}

// Free function for heap-allocated copies
void stock_db_free(stock_t* s) {
    if (s) {
        free(s);
    }
}

// Get all stocks (returns a heap-allocated copy)
stock_t* stock_db_get_all(int* count) {
    pthread_mutex_lock(&db_mutex);
    
    stock_t* stocks_copy = malloc(sizeof(stock_t) * stock_count);
    if (stocks_copy) {
        memcpy(stocks_copy, stocks, sizeof(stock_t) * stock_count);
        *count = stock_count;
    } else {
        *count = 0;
    }

    pthread_mutex_unlock(&db_mutex);
    return stocks_copy;
}

// Get stock by ID (returns a heap-allocated copy)
stock_t* stock_db_get_by_id(uint16_t stock_id) {
    if (stock_id == 0) return NULL;

    stock_t* found_stock = NULL;
    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < stock_count; i++) {
        if (stocks[i].stock_id == stock_id) {
            found_stock = malloc(sizeof(stock_t));
            if (found_stock) {
                memcpy(found_stock, &stocks[i], sizeof(stock_t));
            }
            break;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    return found_stock;
}

// Get stock by symbol (returns a heap-allocated copy)
stock_t* stock_db_get_by_symbol(const char* symbol) {
    if (!symbol) return NULL;

    stock_t* found_stock = NULL;
    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < stock_count; i++) {
        if (strcmp(stocks[i].symbol, symbol) == 0) {
            found_stock = malloc(sizeof(stock_t));
            if (found_stock) {
                memcpy(found_stock, &stocks[i], sizeof(stock_t));
            }
            break;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    return found_stock;
}

// Get the last price for a stock
double stock_db_get_price(uint16_t stock_id) {
    double price = -1.0;
    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < stock_count; i++) {
        if (stocks[i].stock_id == stock_id) {
            price = stocks[i].last_price;
            break;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    return price;
}

// Update stock prices
bool stock_db_update_price(uint16_t stock_id, double new_bid, double new_ask, double new_last) {
    bool success = false;
    pthread_mutex_lock(&db_mutex);

    int found_idx = -1;
    for (int i = 0; i < stock_count; i++) {
        if (stocks[i].stock_id == stock_id) {
            found_idx = i;
            break;
        }
    }

    if (found_idx != -1) {
        stocks[found_idx].best_bid = new_bid;
        stocks[found_idx].best_ask = new_ask;
        stocks[found_idx].last_price = new_last;
        success = true; 
        // No need to persist on every price update for performance reasons
    }

    pthread_mutex_unlock(&db_mutex);
    return success;
}

// Update stock volume and persist
bool stock_db_update_volume(uint16_t stock_id, uint32_t new_volume) {
    bool success = false;
    pthread_mutex_lock(&db_mutex);

    int found_idx = -1;
    for (int i = 0; i < stock_count; i++) {
        if (stocks[i].stock_id == stock_id) {
            found_idx = i;
            break;
        }
    }

    if (found_idx != -1) {
        stocks[found_idx].volume = new_volume;
        // Persist changes to disk after a volume change (trade)
        if (persist_db()) {
            success = true;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    return success;
}