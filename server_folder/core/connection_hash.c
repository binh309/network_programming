#include "../ui/tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "connection_hash.h"
#include "connection_manager.h"

// Hash table buckets - each bucket is a linked list head
static connection_hash_entry_t* hash_buckets[CONN_HASH_SIZE] = {NULL};
static int hash_table_size = 0;  // Current number of entries

/**
 * @brief Simple hash function
 *
 * Maps fd to bucket using modulo
 * For fd=10: hash = 10 % 1024 = 10
 * For fd=1034: hash = 1034 % 1024 = 10 (collision!)
 */
static inline int hash_function(int fd) {
    return fd % CONN_HASH_SIZE;
}

/**
 * @brief Initialize hash table
 *
 * Caller must already hold connections_mutex!
 */
int connection_hash_init(void) {
    server_debug("[CONN_HASH] Initializing hash table with %d buckets\n", CONN_HASH_SIZE);
    
    // memset handles NULL initialization
    memset(hash_buckets, 0, sizeof(hash_buckets));
    hash_table_size = 0;
    
    server_debug("[CONN_HASH] Initialized\n");
    return 0;
}

/**
 * @brief Destroy hash table
 */
void connection_hash_destroy(void) {
    server_debug("[CONN_HASH] Destroying hash table\n");
    
    for (int i = 0; i < CONN_HASH_SIZE; i++) {
        connection_hash_entry_t* entry = hash_buckets[i];
        while (entry) {
            connection_hash_entry_t* next = entry->next;
            free(entry);
            entry = next;
        }
        hash_buckets[i] = NULL;
    }
    
    hash_table_size = 0;
    server_debug("[CONN_HASH] Destroyed\n");
}

/**
 * @brief Insert connection into hash table
 *
 * Average O(1) operation:
 * 1. Compute hash: fd % CONN_HASH_SIZE
 * 2. Create new entry
 * 3. Prepend to bucket's chain (O(1))
 */
int connection_hash_insert(int fd, connection_t* conn) {
    if (!conn) return -1;
    
    int bucket = hash_function(fd);
    
    // Create new entry
    connection_hash_entry_t* entry = malloc(sizeof(connection_hash_entry_t));
    if (!entry) {
        fprintf(stderr, "[CONN_HASH] Allocation failed for fd %d\n", fd);
        return -1;
    }
    
    entry->fd = fd;
    entry->conn = conn;
    
    // Prepend to bucket chain (O(1) operation)
    entry->next = hash_buckets[bucket];
    hash_buckets[bucket] = entry;
    hash_table_size++;
    
    return 0;
}

/**
 * @brief Lookup connection by fd
 *
 * Average O(1) operation:
 * 1. Compute hash: fd % CONN_HASH_SIZE
 * 2. Search collision chain (usually 0-1 items with good distribution)
 * 3. Return connection if found, NULL otherwise
 *
 * With CONN_HASH_SIZE=1024 and ~768 connections:
 * - Load factor: 0.75
 * - Average chain length: < 1
 * - Lookup time: typically 1-2 comparisons
 */
connection_t* connection_hash_lookup(int fd) {
    int bucket = hash_function(fd);
    
    // Search collision chain
    for (connection_hash_entry_t* entry = hash_buckets[bucket]; 
         entry != NULL; 
         entry = entry->next) {
        if (entry->fd == fd) {
            return entry->conn;
        }
    }
    
    return NULL;  // Not found
}

/**
 * @brief Delete connection from hash table
 *
 * Average O(1) operation:
 * 1. Find bucket
 * 2. Search chain for matching fd
 * 3. Remove from chain
 * 4. Free entry
 */
int connection_hash_delete(int fd) {
    int bucket = hash_function(fd);
    
    // Handle first entry in bucket
    if (hash_buckets[bucket] != NULL && hash_buckets[bucket]->fd == fd) {
        connection_hash_entry_t* entry = hash_buckets[bucket];
        hash_buckets[bucket] = entry->next;
        free(entry);
        hash_table_size--;
        return 0;
    }
    
    // Search rest of chain
    for (connection_hash_entry_t* entry = hash_buckets[bucket];
         entry != NULL && entry->next != NULL;
         entry = entry->next) {
        if (entry->next->fd == fd) {
            connection_hash_entry_t* to_remove = entry->next;
            entry->next = to_remove->next;
            free(to_remove);
            hash_table_size--;
            return 0;
        }
    }
    
    return -1;  // Not found
}

/**
 * @brief Get hash table size
 */
int connection_hash_size(void) {
    return hash_table_size;
}

/**
 * @brief Print hash table statistics
 *
 * Shows:
 * - Load factor (entries / buckets)
 * - Collision distribution
 * - Max chain length (longest collision)
 */
void connection_hash_stats(void) {
    int empty_buckets = 0;
    int total_chain_length = 0;
    int max_chain_length = 0;
    
    for (int i = 0; i < CONN_HASH_SIZE; i++) {
        int chain_length = 0;
        for (connection_hash_entry_t* entry = hash_buckets[i];
             entry != NULL;
             entry = entry->next) {
            chain_length++;
        }
        
        if (chain_length == 0) {
            empty_buckets++;
        }
        
        total_chain_length += chain_length;
        if (chain_length > max_chain_length) {
            max_chain_length = chain_length;
        }
    }
    
    double load_factor = (double)hash_table_size / CONN_HASH_SIZE;
    double avg_chain = (double)total_chain_length / (CONN_HASH_SIZE - empty_buckets + 1);
    
    server_debug("[CONN_HASH] Statistics:\n");
    printf("  Entries: %d\n", hash_table_size);
    printf("  Buckets: %d\n", CONN_HASH_SIZE);
    printf("  Load factor: %.2f\n", load_factor);
    printf("  Empty buckets: %d (%.1f%%)\n", empty_buckets, 
           100.0 * empty_buckets / CONN_HASH_SIZE);
    printf("  Avg chain length: %.2f\n", avg_chain);
    printf("  Max chain length: %d\n", max_chain_length);
}
