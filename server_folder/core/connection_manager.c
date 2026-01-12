#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include "connection_manager.h"
#include "connection_hash.h"
#include "../network/packet_parser.h"

// Legacy array - kept for compatibility but not used for lookups
// Use connection_hash_* functions instead for O(1) access
static connection_t* connections[MAX_CONNECTIONS] = {NULL};
static pthread_mutex_t connections_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Initialize the connection manager
 *
 * Initializes:
 * 1. Global mutex for thread-safe connection list updates
 * 2. Hash table for O(1) fd lookups
 */
int connection_mgr_init(void) {
    if (pthread_mutex_init(&connections_mutex, NULL) != 0) {
        fprintf(stderr, "[CONN_MGR] Mutex init failed\n");
        return -1;
    }
    
    // Initialize hash table for O(1) lookups
    if (connection_hash_init() < 0) {
        fprintf(stderr, "[CONN_MGR] Hash table init failed\n");
        pthread_mutex_destroy(&connections_mutex);
        return -1;
    }
    
    printf("[CONN_MGR] Initialized (with O(1) hash table)\n");
    return 0;
}

/**
 * @brief Destroy the connection manager
 *
 * Frees all allocated connections and destroys the mutex
 */
void connection_mgr_destroy(void) {
    pthread_mutex_lock(&connections_mutex);
    
    connection_hash_destroy();  // Destroy hash table first
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i] != NULL) {
            pthread_mutex_destroy(&connections[i]->state_lock);
            free(connections[i]);
            connections[i] = NULL;
        }
    }
    pthread_mutex_unlock(&connections_mutex);
    pthread_mutex_destroy(&connections_mutex);
    printf("[CONN_MGR] Destroyed\n");
}

/**
 * @brief Add a new client connection
 *
 * Allocates space for the connection and initializes it with default values
 * Also adds to hash table for O(1) lookup
 * Thread-safe via mutex
 */
connection_t* connection_mgr_add(int client_socket) {
    pthread_mutex_lock(&connections_mutex);

    // Find an empty slot in legacy array
    int conn_idx = -1;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i] == NULL) {
            conn_idx = i;
            break;
        }
    }

    if (conn_idx == -1) {
        fprintf(stderr, "[CONN_MGR] Max connections reached. Cannot add new client.\n");
        pthread_mutex_unlock(&connections_mutex);
        return NULL;
    }

    // Create and initialize the new connection
    connection_t* new_connection = malloc(sizeof(connection_t));
    if (!new_connection) {
        fprintf(stderr, "[CONN_MGR] Failed to allocate memory for connection\n");
        pthread_mutex_unlock(&connections_mutex);
        return NULL;
    }

    // Initialize per-connection mutex (for protecting read_buffer access)
    if (pthread_mutex_init(&new_connection->state_lock, NULL) != 0) {
        fprintf(stderr, "[CONN_MGR] Failed to initialize per-connection mutex\n");
        free(new_connection);
        pthread_mutex_unlock(&connections_mutex);
        return NULL;
    }

    new_connection->client_socket = client_socket;
    new_connection->state = CONN_ACCEPTING;
    new_connection->user_id = 0;
    new_connection->is_logged_in = false;
    memset(new_connection->username, 0, sizeof(new_connection->username));
    memset(new_connection->read_buffer, 0, sizeof(new_connection->read_buffer));
    new_connection->read_offset = 0;
    new_connection->connection_time = time(NULL);
    
    connections[conn_idx] = new_connection;
    
    // Add to hash table for O(1) lookups by fd
    if (connection_hash_insert(client_socket, new_connection) < 0) {
        fprintf(stderr, "[CONN_MGR] Failed to add connection to hash table\n");
        pthread_mutex_destroy(&new_connection->state_lock);
        free(new_connection);
        connections[conn_idx] = NULL;
        pthread_mutex_unlock(&connections_mutex);
        return NULL;
    }

    pthread_mutex_unlock(&connections_mutex);
    printf("[CONN_MGR] Added new connection for socket %d (state=ACCEPTING, hash_size=%d)\n", 
           client_socket, connection_hash_size());
    return new_connection;
}

/**
 * @brief Get a connection by socket FD - O(1) average via hash table
 *
 * OLD: Linear search through connections array - O(n) with 10K connections
 * NEW: Hash table lookup - O(1) average case with collision chaining
 *
 * Thread-safe via mutex
 */
connection_t* connection_mgr_get(int client_socket) {
    // OPTIMIZATION: Use hash table for O(1) average lookup instead of O(n) linear search
    // Before: With 10K connections, needed to search through entire array
    // After: Hash function gives us bucket directly, search chain (usually 0-1 items)
    
    pthread_mutex_lock(&connections_mutex);
    connection_t* found = connection_hash_lookup(client_socket);
    pthread_mutex_unlock(&connections_mutex);
    return found;
}

/**
 * @brief Remove a connection by socket FD
 *
 * Also removes from hash table for proper cleanup
 * Frees the connection_t and clears the slot
 * Thread-safe via mutex
 */
void connection_mgr_remove(int client_socket) {
    pthread_mutex_lock(&connections_mutex);
    
    // Find and remove from legacy array
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i] && connections[i]->client_socket == client_socket) {
            printf("[CONN_MGR] Removing connection for socket %d, user %s (hash_size=%d->%d)\n", 
                   client_socket, connections[i]->username, 
                   connection_hash_size(), connection_hash_size() - 1);
            
            pthread_mutex_destroy(&connections[i]->state_lock);
            free(connections[i]);
            connections[i] = NULL;
            break;
        }
    }
    
    // Also remove from hash table
    connection_hash_delete(client_socket);
    
    pthread_mutex_unlock(&connections_mutex);
}

/**
 * @brief Get current state of connection (thread-safe)
 *
 * Acquires the per-connection mutex to read state atomically
 */
ConnectionState connection_mgr_get_state(connection_t* conn) {
    if (!conn) return CONN_CLOSED;
    
    pthread_mutex_lock(&conn->state_lock);
    ConnectionState state = conn->state;
    pthread_mutex_unlock(&conn->state_lock);
    
    return state;
}

/**
 * @brief Set connection state (thread-safe)
 *
 * Validates state transitions and updates atomically
 * Valid transitions:
 *   ACCEPTING -> READY
 *   READY -> PROCESSING
 *   PROCESSING -> READY (after request handled)
 *   ANY -> CLOSING
 *   CLOSING -> CLOSED
 */
int connection_mgr_set_state(connection_t* conn, ConnectionState new_state) {
    if (!conn) return -1;
    
    pthread_mutex_lock(&conn->state_lock);
    
    ConnectionState current = conn->state;
    int valid = 0;
    
    // Validate transitions
    switch (current) {
        case CONN_ACCEPTING:
            valid = (new_state == CONN_READY || new_state == CONN_CLOSING);
            break;
        case CONN_READY:
            valid = (new_state == CONN_PROCESSING || new_state == CONN_CLOSING);
            break;
        case CONN_PROCESSING:
            valid = (new_state == CONN_READY || new_state == CONN_CLOSING);
            break;
        case CONN_CLOSING:
            valid = (new_state == CONN_CLOSED);
            break;
        case CONN_CLOSED:
            valid = 0;  // No transitions from CLOSED
            break;
    }
    
    if (!valid) {
        fprintf(stderr, "[CONN_MGR] Invalid state transition: %d -> %d\n", current, new_state);
        pthread_mutex_unlock(&conn->state_lock);
        return -1;
    }
    
    conn->state = new_state;
    printf("[CONN_MGR] Socket %d: state %d -> %d\n", conn->client_socket, current, new_state);
    
    pthread_mutex_unlock(&conn->state_lock);
    return 0;
}

/**
 * @brief Append bytes to connection's read buffer (thread-safe)
 *
 * Checks for buffer overflow and prevents DoS attacks
 */
int connection_mgr_append_data(connection_t* conn, const char* data, int length) {
    if (!conn || !data || length <= 0) return -1;
    
    // NOTE: Caller (request_handler) is responsible for locking
    // DO NOT lock here - mutex is already held by caller
    
    // Check buffer overflow
    if (conn->read_offset + length > BUFFER_SIZE) {
        fprintf(stderr, "[CONN_MGR] Buffer overflow on socket %d (current: %d, adding: %d)\n",
                conn->client_socket, conn->read_offset, length);
        return -1;
    }
    
    // Append data
    memcpy(&conn->read_buffer[conn->read_offset], data, length);
    conn->read_offset += length;
    
    return 0;
}

/**
 * @brief Check if buffer has a complete packet (thread-safe)
 *
 * Does NOT consume the packet - caller must call connection_mgr_consume_packet
 * Also validates packet size to prevent DoS
 */
size_t connection_mgr_has_complete_packet(connection_t* conn) {
    if (!conn) return 0;
    
    pthread_mutex_lock(&conn->state_lock);
    
    const packet_t* pkt = (const packet_t*)conn->read_buffer;

    // Check minimum header size
    if ((size_t)conn->read_offset < sizeof(pkt->header)) {
        pthread_mutex_unlock(&conn->state_lock);
        return 0;
    }
    
    // Peek at header to get declared length
    uint16_t declared_length = ntohs(pkt->header.length);
    
    // Validate packet size (security check)
    if (declared_length > MAX_PACKET_SIZE) {
        fprintf(stderr, "[CONN_MGR] Oversized packet on socket %d: %u bytes (max: %u)\n",
                conn->client_socket, declared_length, MAX_PACKET_SIZE);
        pthread_mutex_unlock(&conn->state_lock);
        return 0;  // Signal error by returning 0 (incomplete)
    }
    
    // Check if full packet in buffer
    size_t total_size = sizeof(pkt->header) + declared_length;
    if ((size_t)conn->read_offset >= total_size) {
        pthread_mutex_unlock(&conn->state_lock);
        return total_size;
    }
    
    pthread_mutex_unlock(&conn->state_lock);
    return 0;  // Incomplete
}

/**
 * @brief Remove processed packet from buffer (thread-safe)
 *
 * Shifts remaining bytes to start of buffer
 */
void connection_mgr_consume_packet(connection_t* conn, size_t packet_size) {
    if (!conn || packet_size == 0) return;
    
    // NOTE: Caller (request_handler) is responsible for locking
    // DO NOT lock here - mutex is already held by caller
    
    if (packet_size >= (size_t)conn->read_offset) {
        // This was the only packet, clear buffer
        conn->read_offset = 0;
    } else {
        // Shift remaining bytes to front
        int remaining = conn->read_offset - packet_size;
        memmove(conn->read_buffer, &conn->read_buffer[packet_size], remaining);
        conn->read_offset = remaining;
    }
}

/**
 * @brief Get raw buffer pointer (caller must lock connection_t->state_lock first!)
 *
 * WARNING: Unsafe - only use if you've acquired state_lock yourself
 */
char* connection_mgr_get_buffer_unsafe(connection_t* conn) {
    if (!conn) return NULL;
    return conn->read_buffer;
}

/**
 * @brief Get current buffer offset (caller must lock connection_t->state_lock first!)
 *
 * WARNING: Unsafe - only use if you've acquired state_lock yourself
 */
int connection_mgr_get_buffer_offset_unsafe(connection_t* conn) {
    if (!conn) return 0;
    return conn->read_offset;
}
/**
 * @brief Check if connection is valid for processing
 *
 * Only CONN_READY and CONN_ACCEPTING are valid states for processing events
 * CONN_CLOSED, CONN_CLOSING should be skipped
 */
int connection_mgr_is_valid_for_processing(connection_t* conn) {
    if (!conn) return 0;
    
    ConnectionState state = connection_mgr_get_state(conn);
    return (state == CONN_READY || state == CONN_ACCEPTING);
}

/**
 * @brief Get human-readable state name
 */
const char* connection_mgr_state_name(ConnectionState state) {
    switch (state) {
        case CONN_ACCEPTING: return "ACCEPTING";
        case CONN_READY: return "READY";
        case CONN_PROCESSING: return "PROCESSING";
        case CONN_CLOSING: return "CLOSING";
        case CONN_CLOSED: return "CLOSED";
        default: return "UNKNOWN";
    }
}
