#ifndef ACCOUNT_DB_H
#define ACCOUNT_DB_H

#include <stdint.h>

// Account record structure
typedef struct {
    uint32_t user_id;
    char username[32];
    char password[32];
    double balance;
    int found;  // 1 if account exists, 0 otherwise
} account_t;

// Function declarations
account_t* account_db_lookup(const char* username, const char* password);
account_t* account_db_get_by_id(uint32_t user_id);
double account_db_get_balance(uint32_t user_id);
int account_db_update_balance(uint32_t user_id, double new_balance);
void account_db_free(account_t* acc);
int account_db_init(const char* db_path);

#endif
