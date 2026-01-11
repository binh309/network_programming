#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "session_manager.h"

// In-memory store for sessions
static session_t* sessions[MAX_SESSIONS] = {NULL};
static pthread_mutex_t sessions_mutex;

// Initialize the session manager
int session_mgr_init(void) {
    if (pthread_mutex_init(&sessions_mutex, NULL) != 0) {
        fprintf(stderr, "[SESSION_MGR] Mutex init failed\n");
        return -1;
    }
    printf("[SESSION_MGR] Initialized\n");
    return 0;
}

// Destroy the session manager and free resources
void session_mgr_destroy(void) {
    pthread_mutex_lock(&sessions_mutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] != NULL) {
            free(sessions[i]);
            sessions[i] = NULL;
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
    pthread_mutex_destroy(&sessions_mutex);
    printf("[SESSION_MGR] Destroyed\n");
}

// Add a new client session
session_t* session_mgr_add(int client_socket) {
    pthread_mutex_lock(&sessions_mutex);

    // Find an empty slot
    int session_idx = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] == NULL) {
            session_idx = i;
            break;
        }
    }

    if (session_idx == -1) {
        fprintf(stderr, "[SESSION_MGR] Max sessions reached. Cannot add new client.\n");
        pthread_mutex_unlock(&sessions_mutex);
        return NULL;
    }

    // Create and initialize the new session
    session_t* new_session = malloc(sizeof(session_t));
    if (!new_session) {
        pthread_mutex_unlock(&sessions_mutex);
        return NULL;
    }

    new_session->client_socket = client_socket;
    new_session->user_id = 0;
    new_session->is_logged_in = false;
    memset(new_session->username, 0, sizeof(new_session->username));
    memset(new_session->read_buffer, 0, sizeof(new_session->read_buffer));
    new_session->read_offset = 0;
    
    sessions[session_idx] = new_session;

    pthread_mutex_unlock(&sessions_mutex);
    printf("[SESSION_MGR] Added new session for socket %d\n", client_socket);
    return new_session;
}

// Get a session by client socket
session_t* session_mgr_get(int client_socket) {
    session_t* found_session = NULL;
    pthread_mutex_lock(&sessions_mutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] && sessions[i]->client_socket == client_socket) {
            found_session = sessions[i];
            break;
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
    return found_session;
}

// Log out a client by socket
void session_mgr_logout(int client_socket) {
    pthread_mutex_lock(&sessions_mutex);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] && sessions[i]->client_socket == client_socket) {
            printf("[SESSION_MGR] Logging out session for socket %d, user %s\n", client_socket, sessions[i]->username);
            // Free the session slot
            free(sessions[i]);
            sessions[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
}