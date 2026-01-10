#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "session_manager.h"

static client_session_t sessions[MAX_CLIENTS];
static int session_count = 0;
static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;

// Initialize session manager
int session_mgr_init(void) {
    pthread_mutex_lock(&session_lock);
    session_count = 0;
    memset(sessions, 0, sizeof(sessions));
    pthread_mutex_unlock(&session_lock);
    printf("[SESSION_MGR] Initialized\n");
    return 0;
}

// Authenticate a client
int session_mgr_authenticate(int client_fd, uint32_t user_id, const char* username) {
    pthread_mutex_lock(&session_lock);
    
    // Check if already authenticated
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].client_fd == client_fd) {
            sessions[i].user_id = user_id;
            sessions[i].authenticated = 1;
            strncpy(sessions[i].username, username, 31);
            sessions[i].username[31] = '\0';
            pthread_mutex_unlock(&session_lock);
            printf("[SESSION_MGR] Client fd=%d authenticated as user %u (%s)\n", client_fd, user_id, username);
            return 0;
        }
    }
    
    // Add new session
    if (session_count >= MAX_CLIENTS) {
        printf("[SESSION_MGR] Max clients reached\n");
        pthread_mutex_unlock(&session_lock);
        return -1;
    }
    
    client_session_t* session = &sessions[session_count];
    session->client_fd = client_fd;
    session->user_id = user_id;
    session->authenticated = 1;
    strncpy(session->username, username, 31);
    session->username[31] = '\0';
    session_count++;
    
    printf("[SESSION_MGR] New session created: fd=%d, user=%u (%s)\n", client_fd, user_id, username);
    pthread_mutex_unlock(&session_lock);
    return 0;
}

// Check if client is authenticated
int session_mgr_is_authenticated(int client_fd, uint32_t* user_id) {
    pthread_mutex_lock(&session_lock);
    
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].client_fd == client_fd && sessions[i].authenticated) {
            *user_id = sessions[i].user_id;
            pthread_mutex_unlock(&session_lock);
            return 1;
        }
    }
    
    pthread_mutex_unlock(&session_lock);
    return 0;
}

// Logout a client
int session_mgr_logout(int client_fd) {
    pthread_mutex_lock(&session_lock);
    
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].client_fd == client_fd) {
            printf("[SESSION_MGR] Client fd=%d logged out\n", client_fd);
            // Shift remaining sessions
            for (int j = i; j < session_count - 1; j++) {
                sessions[j] = sessions[j + 1];
            }
            session_count--;
            pthread_mutex_unlock(&session_lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&session_lock);
    return -1;
}

// Cleanup session manager
void session_mgr_cleanup(void) {
    pthread_mutex_lock(&session_lock);
    session_count = 0;
    memset(sessions, 0, sizeof(sessions));
    pthread_mutex_unlock(&session_lock);
    printf("[SESSION_MGR] Cleaned up\n");
}
