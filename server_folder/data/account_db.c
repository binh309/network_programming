#include "ui/tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "account_db.h"
#include "../model/error.h"

#define MAX_ACCOUNTS 2000  // Support 1000 test accounts + 1000 regular users

// In-memory database
static account_t accounts[MAX_ACCOUNTS];
static int num_accounts = 0;
static pthread_mutex_t db_mutex;
static const char* DB_PATH = "data/accounts.txt";

// Simple password hashing (XOR with a key)
static void hash_password(const char* password, char* hashed_password) {
    // Use plaintext to match accounts.txt and avoid binary issues in text file
    strncpy(hashed_password, password, 31);
    hashed_password[31] = '\0';
}

// Function to write the entire in-memory database to the file
static bool persist_db() {
    FILE* fp = fopen(DB_PATH, "w");
    if (!fp) {
        fprintf(stderr, "[DB] Error opening accounts file for writing: %s\n", DB_PATH);
        return false;
    }

    for (int i = 0; i < num_accounts; ++i) {
        fprintf(fp, "%u,%s,%s,%.2f\n",
                accounts[i].user_id,
                accounts[i].username,
                accounts[i].password,
                accounts[i].balance);
    }

    fclose(fp);
    return true;
}

// Parse a line from accounts.txt: user_id,username,password,balance
static bool parse_account_line(const char* line) {
    if (num_accounts >= MAX_ACCOUNTS) {
        fprintf(stderr, "[DB] Max accounts reached, cannot load more.\n");
        return false;
    }

    account_t* acc = &accounts[num_accounts];
    
    char line_copy[256];
    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    char* saveptr;
    char* token = strtok_r(line_copy, ",\n", &saveptr);
    if (!token) return false;
    acc->user_id = atoi(token);

    token = strtok_r(NULL, ",\n", &saveptr);
    if (!token) return false;
    strncpy(acc->username, token, sizeof(acc->username) - 1);
    acc->username[sizeof(acc->username) - 1] = '\0';

    token = strtok_r(NULL, ",\n", &saveptr);
    if (!token) return false;
    strncpy(acc->password, token, sizeof(acc->password) - 1);
    acc->password[sizeof(acc->password) - 1] = '\0';

    token = strtok_r(NULL, ",\n", &saveptr);
    if (!token) return false;
    acc->balance = atof(token);
    
    num_accounts++;
    return true;
}

// Initialize database
bool account_db_init(const char* db_path) {
    if (pthread_mutex_init(&db_mutex, NULL) != 0) {
        fprintf(stderr, "[DB] Mutex init failed\n");
        return false;
    }

    const char* path = db_path ? db_path : DB_PATH;
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "[DB] Cannot open accounts database: %s\n", path);
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (!parse_account_line(line)) {
            fprintf(stderr, "[DB] Failed to parse line: %s", line);
        }
    }

    fclose(fp);
    server_debug("[DB] Accounts database initialized with %d accounts\n", num_accounts);
    return true;
}

// Free account structure
void account_db_free(account_t* acc) {
    if (acc) {
        free(acc);
    }
}

// Check if a username exists
bool account_db_username_exists(const char* username) {
    bool exists = false;
    pthread_mutex_lock(&db_mutex);
    for (int i = 0; i < num_accounts; ++i) {
        if (strcmp(accounts[i].username, username) == 0) {
            exists = true;
            break;
        }
    }
    pthread_mutex_unlock(&db_mutex);
    return exists;
}

// Add a new user to the database
bool account_db_add(const char* username, const char* password) {
    if (!username || !password) {
        return false;
    }

    pthread_mutex_lock(&db_mutex);

    if (num_accounts >= MAX_ACCOUNTS) {
        fprintf(stderr, "[DB] Cannot add new user, database is full.\n");
        pthread_mutex_unlock(&db_mutex);
        return false;
    }

    for (int i = 0; i < num_accounts; ++i) {
        if (strcmp(accounts[i].username, username) == 0) {
            fprintf(stderr, "[DB] Username '%s' already exists.\n", username);
            pthread_mutex_unlock(&db_mutex);
            return false;
        }
    }

    account_t* new_acc = &accounts[num_accounts];
    
    // Find next available regular user ID (below TEST_ACCOUNT_ID_START)
    // Don't use last account's ID + 1 because test accounts have IDs 9000+
    uint32_t max_regular_id = 0;
    for (int i = 0; i < num_accounts; i++) {
        if (accounts[i].user_id < TEST_ACCOUNT_ID_START && accounts[i].user_id > max_regular_id) {
            max_regular_id = accounts[i].user_id;
        }
    }
    new_acc->user_id = max_regular_id + 1;
    
    // Safety check: don't overlap with test account range
    if (new_acc->user_id >= TEST_ACCOUNT_ID_START) {
        fprintf(stderr, "[DB] Cannot add new user, regular user ID range exhausted.\n");
        pthread_mutex_unlock(&db_mutex);
        return false;
    }
    
    snprintf(new_acc->username, sizeof(new_acc->username), "%s", username);
    
    char hashed_pass[32];
    hash_password(password, hashed_pass);
    snprintf(new_acc->password, sizeof(new_acc->password), "%s", hashed_pass);

    new_acc->balance = 10000.0; // Default starting balance

    num_accounts++;

    bool success = persist_db();
    pthread_mutex_unlock(&db_mutex);

    if (success) {
        server_debug("[DB] Added new user: %s\n", username);
    }

    return success;
}

// Lookup account by username and password
account_t* account_db_lookup(const char* username, const char* password) {
    if (!username || !password) {
        return NULL;
    }

    char hashed_pass[32];
    hash_password(password, hashed_pass);

    account_t* found_acc = NULL;
    pthread_mutex_lock(&db_mutex);
    for (int i = 0; i < num_accounts; ++i) {
        if (strcmp(accounts[i].username, username) == 0 && strcmp(accounts[i].password, hashed_pass) == 0) {
            found_acc = malloc(sizeof(account_t));
            if (found_acc) {
                memcpy(found_acc, &accounts[i], sizeof(account_t));
            }
            break;
        }
    }
    pthread_mutex_unlock(&db_mutex);

    return found_acc;
}

// Get account by user ID
account_t* account_db_get_by_id(uint32_t user_id) {
    if (user_id == 0) return NULL;

    account_t* found_acc = NULL;
    pthread_mutex_lock(&db_mutex);
    for (int i = 0; i < num_accounts; ++i) {
        if (accounts[i].user_id == user_id) {
            found_acc = malloc(sizeof(account_t));
            if (found_acc) {
                memcpy(found_acc, &accounts[i], sizeof(account_t));
            }
            break;
        }
    }
    pthread_mutex_unlock(&db_mutex);

    return found_acc;
}

// Get balance for a user
double account_db_get_balance(uint32_t user_id) {
    double balance = -1.0;
    pthread_mutex_lock(&db_mutex);
    for (int i = 0; i < num_accounts; ++i) {
        if (accounts[i].user_id == user_id) {
            balance = accounts[i].balance;
            break;
        }
    }
    pthread_mutex_unlock(&db_mutex);
    return balance;
}

// Update balance for a user
bool account_db_update_balance(uint32_t user_id, double new_balance) {
    bool success = false;
    pthread_mutex_lock(&db_mutex);

    int found_idx = -1;
    for (int i = 0; i < num_accounts; i++) {
        if (accounts[i].user_id == user_id) {
            found_idx = i;
            break;
        }
    }

    if (found_idx != -1) {
        accounts[found_idx].balance = new_balance;
        if (persist_db()) {
            success = true;
            server_debug("[DB] Updated balance for user %u to %.2f\n", user_id, new_balance);
        }
    } else {
        fprintf(stderr, "[DB] User ID %u not found for balance update\n", user_id);
    }
    
    pthread_mutex_unlock(&db_mutex);
    return success;
}

// Delete all test accounts (ID range 9001-9100)
int account_db_delete_test_accounts(void) {
    int deleted = 0;
    pthread_mutex_lock(&db_mutex);
    
    // Remove test accounts by shifting array
    int write_idx = 0;
    for (int read_idx = 0; read_idx < num_accounts; read_idx++) {
        if (accounts[read_idx].user_id >= TEST_ACCOUNT_ID_START && 
            accounts[read_idx].user_id <= TEST_ACCOUNT_ID_END) {
            deleted++;
        } else {
            if (write_idx != read_idx) {
                accounts[write_idx] = accounts[read_idx];
            }
            write_idx++;
        }
    }
    num_accounts = write_idx;
    
    persist_db();
    pthread_mutex_unlock(&db_mutex);
    return deleted;
}

// Create a test account with specific ID
bool account_db_create_test_account(uint32_t id, const char* username, 
                                     const char* password, double balance) {
    pthread_mutex_lock(&db_mutex);
    
    if (num_accounts >= MAX_ACCOUNTS) {
        pthread_mutex_unlock(&db_mutex);
        return false;
    }
    
    account_t* new_acc = &accounts[num_accounts];
    new_acc->user_id = id;
    snprintf(new_acc->username, sizeof(new_acc->username), "%s", username);
    snprintf(new_acc->password, sizeof(new_acc->password), "%s", password);
    new_acc->balance = balance;
    
    num_accounts++;
    pthread_mutex_unlock(&db_mutex);
    return true;
}

// Setup all test accounts (delete existing, create fresh with $1M each)
int account_db_setup_test_accounts(void) {
    // First delete any existing test accounts
    account_db_delete_test_accounts();
    
    // Create 100 test accounts
    int created = 0;
    for (int i = 1; i <= TEST_ACCOUNT_COUNT; i++) {
        char username[32], password[32];
        snprintf(username, sizeof(username), "test%d", i);
        snprintf(password, sizeof(password), "test%d", i);
        
        if (account_db_create_test_account(
                TEST_ACCOUNT_ID_START + i - 1, 
                username, 
                password, 
                TEST_ACCOUNT_BALANCE)) {
            created++;
        }
    }
    
    // Persist to disk
    pthread_mutex_lock(&db_mutex);
    persist_db();
    pthread_mutex_unlock(&db_mutex);
    
    return created;
}