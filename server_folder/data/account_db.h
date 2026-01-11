#ifndef ACCOUNT_DB_H
#define ACCOUNT_DB_H

#include <stdint.h>
#include <stdbool.h>

// Account record structure
typedef struct {
    uint32_t user_id;
    char username[32];
    char password[32];
    double balance;
} account_t;

// Function declarations
bool account_db_init(const char* db_path);
void account_db_free(account_t* acc);

// Thread-safe functions
account_t* account_db_lookup(const char* username, const char* password);
account_t* account_db_get_by_id(uint32_t user_id);
bool account_db_username_exists(const char* username);
double account_db_get_balance(uint32_t user_id);
bool account_db_update_balance(uint32_t user_id, double new_balance);
bool account_db_add(const char* username, const char* password);

#endif
