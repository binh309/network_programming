#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "server.h" // For BUFFER_SIZE

#define MAX_SESSIONS 100

// Represents a client session
typedef struct {
    int client_socket;
    uint32_t user_id;
    char username[32];
    bool is_logged_in;
    char read_buffer[BUFFER_SIZE];
    int read_offset;
} session_t;

// Function declarations
int session_mgr_init(void);
void session_mgr_destroy(void);

session_t* session_mgr_add(int client_socket);
session_t* session_mgr_get(int client_socket);
void session_mgr_logout(int client_socket);

#endif
