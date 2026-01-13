#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define MAX_CONNECTIONS 2000
#define BUFFER_SIZE 4096

/**
 * @brief Connection lifecycle states
 *
 * Prevents invalid state transitions (e.g., reading from CLOSED connection)
 */
typedef enum {
    CONN_ACCEPTING,     // Just accepted, not yet in event loop
    CONN_READY,         // Waiting for data
    CONN_PROCESSING,    // Currently handling request
    CONN_CLOSING,       // Shutdown initiated
    CONN_CLOSED         // Fully cleaned up (or error)
} ConnectionState;

/**
 * @brief Represents a client connection with its state
 *
 * Tracks:
 * - Socket descriptor for this client
 * - Authenticated user info (user_id, username)
 * - Partial packet buffer for incomplete messages
 * - Connection lifecycle state
 * - Connection metadata (login time)
 * - Thread safety (per-connection mutex)
 */
typedef struct {
    // === Network ===
    int client_socket;                  // Socket FD for this client
    ConnectionState state;              // Current connection state
    pthread_mutex_t state_lock;         // Protects this entire struct from concurrent access
    
    // === Session ===
    uint32_t user_id;                   // User ID after login (0 if not logged in)
    char username[32];                  // Username after login
    bool is_logged_in;                  // Whether this connection is authenticated
    
    // === Buffering ===
    char read_buffer[BUFFER_SIZE];      // Buffer for incomplete packets
    int read_offset;                    // How many bytes in buffer so far
    
    // === Metadata ===
    time_t connection_time;             // When this connection was established
    time_t last_activity_time;          // Last time this connection had activity (for idle timeout)
} connection_t;

/**
 * @brief Initialize the connection manager
 *
 * Sets up mutex and internal data structures
 *
 * @return 0 on success, -1 on error
 */
int connection_mgr_init(void);

/**
 * @brief Destroy the connection manager
 *
 * Frees all connections and destroys mutex
 */
void connection_mgr_destroy(void);

/**
 * @brief Add a new client connection
 *
 * Allocates a connection_t and registers the socket
 * Should be called when client first connects
 *
 * @param client_socket The accepted socket FD
 * @return Pointer to connection_t on success, NULL if max connections reached
 */
connection_t* connection_mgr_add(int client_socket);

/**
 * @brief Get an existing connection by socket FD
 *
 * Used to look up session state for an active connection
 *
 * @param client_socket The socket FD to search for
 * @return Pointer to connection_t if found, NULL otherwise
 */
connection_t* connection_mgr_get(int client_socket);

/**
 * @brief Remove a closed connection
 *
 * Called when client disconnects (EOF or error)
 * Frees the connection_t and socket resources
 *
 * @param client_socket The socket FD to remove
 */
void connection_mgr_remove(int client_socket);

/**
 * @brief Get current state of connection (thread-safe)
 *
 * Locks connection mutex to read state atomically
 *
 * @param conn The connection
 * @return The current ConnectionState
 */
ConnectionState connection_mgr_get_state(connection_t* conn);

/**
 * @brief Set connection state (thread-safe)
 *
 * Locks connection mutex to write state atomically
 * Validates state transitions (e.g., can't go CLOSED -> READY)
 *
 * @param conn The connection
 * @param new_state The desired state
 * @return 0 on valid transition, -1 on invalid
 */
int connection_mgr_set_state(connection_t* conn, ConnectionState new_state);

/**
 * @brief Append bytes to connection's read buffer (thread-safe)
 *
 * Locks connection mutex while modifying buffer
 *
 * @param conn The connection
 * @param data Bytes to append
 * @param length Number of bytes
 * @return 0 on success, -1 if buffer would overflow
 */
int connection_mgr_append_data(connection_t* conn, const char* data, int length);

/**
 * @brief Check if buffer has a complete packet (thread-safe)
 *
 * Locks connection mutex while checking
 * Does NOT consume the packet - caller must call connection_mgr_consume_packet
 *
 * @param conn The connection
 * @return Size of first complete packet, or 0 if incomplete
 */
size_t connection_mgr_has_complete_packet(connection_t* conn);

/**
 * @brief Remove processed packet from buffer (thread-safe)
 *
 * Shifts remaining bytes to start of buffer
 * Locks connection mutex while modifying buffer
 *
 * @param conn The connection
 * @param packet_size Size of packet to remove
 */
void connection_mgr_consume_packet(connection_t* conn, size_t packet_size);

/**
 * @brief Get raw buffer pointer (caller must lock connection_t->state_lock first!)
 *
 * WARNING: Caller is responsible for acquiring connection_t->state_lock before calling
 * and not releasing it until done reading/writing buffer
 *
 * @param conn The connection
 * @return Pointer to read_buffer
 */
char* connection_mgr_get_buffer_unsafe(connection_t* conn);

/**
 * @brief Get current buffer offset (caller must lock connection_t->state_lock first!)
 *
 * @param conn The connection
 * @return Current read_offset
 */
int connection_mgr_get_buffer_offset_unsafe(connection_t* conn);

/**
 * @brief Check if connection is in a valid state for processing
 *
 * Valid states: CONN_READY, CONN_ACCEPTING
 * Invalid states: CONN_CLOSED, CONN_CLOSING, CONN_PROCESSING (should not get event)
 *
 * @param conn The connection
 * @return 1 if valid for processing, 0 if invalid
 */
int connection_mgr_is_valid_for_processing(connection_t* conn);

/**
 * @brief Get human-readable state name for logging
 *
 * @param state The ConnectionState
 * @return String like "READY", "CLOSED", etc.
 */
const char* connection_mgr_state_name(ConnectionState state);

/**
 * @brief Update the last activity timestamp for a connection
 *
 * Call this when the connection receives data or processes a request
 *
 * @param conn The connection
 */
void connection_mgr_update_activity(connection_t* conn);

/**
 * @brief Check if a connection has been idle for more than timeout seconds
 *
 * @param conn The connection
 * @param timeout_seconds Maximum seconds of inactivity allowed
 * @return 1 if idle, 0 if recently active
 */
int connection_mgr_is_idle(connection_t* conn, int timeout_seconds);

#endif // CONNECTION_MANAGER_H
