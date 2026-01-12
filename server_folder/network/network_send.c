#include "network_send.h"
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>

// A mutex to ensure thread-safe writes to sockets.
static pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Sends a block of data over a socket, ensuring all bytes are sent.
 * 
 * This is a thread-safe wrapper for sending data. It handles partial sends
 * and common non-blocking socket errors (EAGAIN, EWOULDBLOCK).
 * 
 * @param sockfd The socket file descriptor to write to.
 * @param buffer The data to send.
 * @param length The number of bytes to send.
 * @return 0 on success, -1 on error.
 */
int network_send(int sockfd, const void* buffer, size_t length) {
    if (buffer == NULL || length == 0) {
        return 0; // Nothing to send
    }

    size_t total_sent = 0;
    
    pthread_mutex_lock(&send_mutex);

    while (total_sent < length) {
        ssize_t sent = write(sockfd, (const char*)buffer + total_sent, length - total_sent);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Non-blocking socket would block, let the caller handle it (e.g., by retrying later)
                // For a simple synchronous send, we can treat it as an error.
                // Or, we could implement a more complex retry mechanism.
                perror("network_send: write would block");
                pthread_mutex_unlock(&send_mutex);
                return -1; 
            }
            perror("network_send: write failed");
            pthread_mutex_unlock(&send_mutex);
            return -1;
        }
        if (sent == 0) {
            fprintf(stderr, "network_send: Connection closed by peer during send.\n");
            pthread_mutex_unlock(&send_mutex);
            return -1;
        }
        total_sent += sent;
    }

    pthread_mutex_unlock(&send_mutex);
    return 0;
}
