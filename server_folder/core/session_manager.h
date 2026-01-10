#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <stdint.h>
#include <pthread.h>

// Client session
typedef struct {
    int client_fd;
    uint32_t user_id;
    char username[32];
    int authenticated;
} client_session_t;

#define MAX_CLIENTS 100

// Function declarations
int session_mgr_init(void);
int session_mgr_authenticate(int client_fd, uint32_t user_id, const char* username);
int session_mgr_is_authenticated(int client_fd, uint32_t* user_id);
int session_mgr_logout(int client_fd);
void session_mgr_cleanup(void);

#endif
