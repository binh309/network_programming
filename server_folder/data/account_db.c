#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account_db.h"

static const char* DB_PATH = "server_folder/data/accounts.txt";

// Parse a line from accounts.txt: user_id,username,password,balance
static account_t* parse_account_line(const char* line) {
    account_t* acc = malloc(sizeof(account_t));
    if (!acc) return NULL;

    // Buffer for parsing
    char line_copy[256];
    strncpy(line_copy, line, 255);
    line_copy[255] = '\0';

    // Parse CSV: id,username,password,balance
    char* saveptr;
    char* token = strtok_r(line_copy, ",", &saveptr);
    if (!token) {
        free(acc);
        return NULL;
    }

    acc->user_id = atoi(token);

    token = strtok_r(NULL, ",", &saveptr);
    if (!token) {
        free(acc);
        return NULL;
    }
    strncpy(acc->username, token, 31);
    acc->username[31] = '\0';

    token = strtok_r(NULL, ",", &saveptr);
    if (!token) {
        free(acc);
        return NULL;
    }
    strncpy(acc->password, token, 31);
    acc->password[31] = '\0';

    token = strtok_r(NULL, ",", &saveptr);
    if (!token) {
        free(acc);
        return NULL;
    }
    acc->balance = atof(token);

    acc->found = 1;
    return acc;
}

// Lookup account by username and password
account_t* account_db_lookup(const char* username, const char* password) {
    if (!username || !password) {
        return NULL;
    }

    FILE* fp = fopen(DB_PATH, "r");
    if (!fp) {
        fprintf(stderr, "[DB] Error opening accounts file: %s\n", DB_PATH);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        account_t* acc = parse_account_line(line);
        if (!acc) continue;

        // Check if username and password match
        if (strcmp(acc->username, username) == 0 && strcmp(acc->password, password) == 0) {
            fclose(fp);
            return acc;  // Found!
        }

        free(acc);
    }

    fclose(fp);

    // Not found - return account with found=0
    account_t* not_found = malloc(sizeof(account_t));
    if (not_found) {
        not_found->found = 0;
    }
    return not_found;
}

// Free account structure
void account_db_free(account_t* acc) {
    if (acc) {
        free(acc);
    }
}

// Initialize database (just validate file exists)
int account_db_init(const char* db_path) {
    FILE* fp = fopen(db_path ? db_path : DB_PATH, "r");
    if (!fp) {
        fprintf(stderr, "[DB] Cannot open accounts database\n");
        return -1;
    }
    fclose(fp);
    printf("[DB] Accounts database initialized\n");
    return 0;
}

// Get account by user ID
account_t* account_db_get_by_id(uint32_t user_id) {
    if (user_id == 0) return NULL;

    FILE* fp = fopen(DB_PATH, "r");
    if (!fp) {
        fprintf(stderr, "[DB] Error opening accounts file: %s\n", DB_PATH);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        account_t* acc = parse_account_line(line);
        if (!acc) continue;

        if (acc->user_id == user_id) {
            fclose(fp);
            return acc;
        }

        free(acc);
    }

    fclose(fp);
    return NULL;
}

// Get balance for a user
double account_db_get_balance(uint32_t user_id) {
    account_t* acc = account_db_get_by_id(user_id);
    if (!acc) return 0.0;
    double balance = acc->balance;
    account_db_free(acc);
    return balance;
}

// Update balance for a user (in-memory only, persists in file at next login)
int account_db_update_balance(uint32_t user_id, double new_balance) {
    // For simplicity, we'll update the file directly
    // Read all accounts
    FILE* fp = fopen(DB_PATH, "r");
    if (!fp) {
        fprintf(stderr, "[DB] Cannot open accounts file for update\n");
        return -1;
    }

    char lines[100][256];
    int line_count = 0;
    char temp_line[256];

    while (fgets(temp_line, sizeof(temp_line), fp) && line_count < 100) {
        strcpy(lines[line_count], temp_line);
        line_count++;
    }
    fclose(fp);

    // Update the target user
    int found = 0;
    for (int i = 0; i < line_count; i++) {
        account_t* acc = parse_account_line(lines[i]);
        if (acc && acc->user_id == user_id) {
            // Reconstruct the line with new balance
            snprintf(lines[i], 256, "%u,%s,%s,%.2f\n", 
                     user_id, acc->username, acc->password, new_balance);
            found = 1;
            free(acc);
            break;
        }
        if (acc) free(acc);
    }

    if (!found) {
        fprintf(stderr, "[DB] User ID %u not found for balance update\n", user_id);
        return -1;
    }

    // Write back to file
    fp = fopen(DB_PATH, "w");
    if (!fp) {
        fprintf(stderr, "[DB] Cannot open accounts file for writing\n");
        return -1;
    }

    for (int i = 0; i < line_count; i++) {
        fputs(lines[i], fp);
    }
    fclose(fp);

    printf("[DB] Updated balance for user %u to %.2f\n", user_id, new_balance);
    return 0;
}
