#include "ui/tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "transaction_db.h"

#define MAX_TRANSACTIONS 10000

static transaction_t transactions[MAX_TRANSACTIONS];
static uint32_t transaction_count = 0;
static uint32_t next_order_id = 1001;
static pthread_mutex_t transaction_lock = PTHREAD_MUTEX_INITIALIZER;

// Initialize transaction database
int transaction_db_init(void) {
    pthread_mutex_lock(&transaction_lock);
    transaction_count = 0;
    next_order_id = 1001;
    pthread_mutex_unlock(&transaction_lock);
    server_debug("[TRANSACTION_DB] Initialized\n");
    return 0;
}

// Record a transaction and return the order ID
uint32_t transaction_db_record(uint32_t user_id, uint8_t type, uint16_t stock_id, 
                               uint32_t quantity, double price) {
    pthread_mutex_lock(&transaction_lock);
    
    if (transaction_count >= MAX_TRANSACTIONS) {
        server_debug("[TRANSACTION_DB] Transaction limit reached\n");
        pthread_mutex_unlock(&transaction_lock);
        return (uint32_t)-1;
    }
    
    transaction_t* tx = &transactions[transaction_count];
    tx->order_id = next_order_id++;
    tx->user_id = user_id;
    tx->type = type;
    tx->stock_id = stock_id;
    tx->quantity = quantity;
    tx->price = price;
    tx->total_amount = (double)quantity * price;
    tx->timestamp = time(NULL);
    
    transaction_count++;
    
    server_debug("[TRANSACTION_DB] Recorded: Order ID=%u, User=%u, Type=%s, Stock=%u, Qty=%u, Price=%.2f\n",
           tx->order_id, user_id, (type == TRANSACTION_BUY ? "BUY" : "SELL"), 
           stock_id, quantity, price);
    
    uint32_t order_id = tx->order_id;
    pthread_mutex_unlock(&transaction_lock);
    return order_id;
}

// Print all transactions (for debugging)
void transaction_db_print_all(void) {
    pthread_mutex_lock(&transaction_lock);
    printf("\n[TRANSACTIONS] Total: %u\n", transaction_count);
    for (uint32_t i = 0; i < transaction_count; i++) {
        transaction_t* tx = &transactions[i];
        printf("  [%u] Order ID=%u, User=%u, %s, Stock=%u, Qty=%u, Price=%.2f, Total=%.2f\n",
               i, tx->order_id, tx->user_id, 
               (tx->type == TRANSACTION_BUY ? "BUY " : "SELL"), 
               tx->stock_id, tx->quantity, tx->price, tx->total_amount);
    }
    pthread_mutex_unlock(&transaction_lock);
}

// Print transactions for a specific user
void transaction_db_print_user(uint32_t user_id) {
    pthread_mutex_lock(&transaction_lock);
    printf("\n[TRANSACTIONS] User %u:\n", user_id);
    int count = 0;
    for (uint32_t i = 0; i < transaction_count; i++) {
        transaction_t* tx = &transactions[i];
        if (tx->user_id == user_id) {
            printf("  [%d] Order ID=%u, %s, Stock=%u, Qty=%u, Price=%.2f\n",
                   count++, tx->order_id, 
                   (tx->type == TRANSACTION_BUY ? "BUY " : "SELL"), 
                   tx->stock_id, tx->quantity, tx->price);
        }
    }
    if (count == 0) {
        printf("  (no transactions)\n");
    }
    pthread_mutex_unlock(&transaction_lock);
}
