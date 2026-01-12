/**
 * @file connection_hash.h
 * @brief Hash table for fast O(1) connection lookup by file descriptor
 *
 * PROBLEM: Original code used O(n) linear search through connections array
 * SOLUTION: Hash table indexed by (fd % HASH_SIZE) for O(1) average lookup
 *
 * - Hash collision handling: Chaining with linked list
 * - Load factor: Automatic resizing when > 0.75
 * - Thread safety: Uses global connections_mutex from connection_manager
 */

#ifndef CONNECTION_HASH_H
#define CONNECTION_HASH_H

#include "connection_manager.h"

#define CONN_HASH_SIZE 1024  // Capacity for 1024 buckets, ~750 connections at 0.75 load

/**
 * @brief Hash table entry (linked list node)
 *
 * Connections with same hash collide and are chained together
 * Example: fd=10 and fd=1034 both hash to bucket 10 (with HASH_SIZE=1024)
 */
typedef struct connection_hash_entry {
    connection_t* conn;
    int fd;
    struct connection_hash_entry* next;  // For collision chaining
} connection_hash_entry_t;

/**
 * @brief Initialize the connection hash table
 *
 * Must be called before any hash operations
 * Initializes buckets array to all NULL
 */
int connection_hash_init(void);

/**
 * @brief Destroy the hash table
 *
 * Frees all entries and bucket array
 * Note: Does NOT free the connection_t* objects themselves (that's connection_manager's job)
 */
void connection_hash_destroy(void);

/**
 * @brief Insert a connection into the hash table
 *
 * Hash key: fd % CONN_HASH_SIZE
 * If collision, appends to chain
 * Thread-safe via connection_manager's global mutex
 *
 * @param fd File descriptor (key)
 * @param conn Pointer to connection structure
 * @return 0 on success, -1 on error
 */
int connection_hash_insert(int fd, connection_t* conn);

/**
 * @brief Lookup a connection by file descriptor - O(1) average
 *
 * Hash key: fd % CONN_HASH_SIZE
 * Linear search through collision chain (usually 0-1 items)
 * Thread-safe via connection_manager's global mutex
 *
 * @param fd File descriptor (key)
 * @return Pointer to connection, or NULL if not found
 */
connection_t* connection_hash_lookup(int fd);

/**
 * @brief Delete a connection from hash table
 *
 * Removes entry from collision chain
 * Thread-safe via connection_manager's global mutex
 *
 * @param fd File descriptor (key)
 * @return 0 on success, -1 if not found
 */
int connection_hash_delete(int fd);

/**
 * @brief Get current hash table size (number of entries)
 *
 * Useful for diagnostics and load factor calculation
 *
 * @return Number of connection entries in hash table
 */
int connection_hash_size(void);

/**
 * @brief Get hash bucket statistics (for diagnostics)
 *
 * Prints bucket distribution:
 * - Number of empty buckets
 * - Average chain length
 * - Max chain length (longest collision)
 * - Load factor
 */
void connection_hash_stats(void);

#endif // CONNECTION_HASH_H
