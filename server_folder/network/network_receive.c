#include "network_receive.h"
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/**
 * @brief Receives data from a socket into a buffer.
 *
 * This function attempts to read up to `buffer_size` bytes from the given 
 * socket. It is designed to work with non-blocking sockets and handles
 * common errors like EAGAIN and EWOULDBLOCK.
 *
 * @param sockfd The socket file descriptor to read from.
 * @param buffer The buffer to store the received data.
 * @param buffer_size The maximum number of bytes to read.
 * @return The number of bytes read, 0 if the connection was closed gracefully, 
 *         or -1 on error. If an error occurs, the value of errno is preserved
 *         so the caller can check for EAGAIN/EWOULDBLOCK.
 */
ssize_t network_receive(int sockfd, void* buffer, size_t buffer_size) {
    ssize_t bytes_read = read(sockfd, buffer, buffer_size);

    if (bytes_read < 0) {
        // Error occurred. Caller is responsible for checking errno,
        // especially for EAGAIN or EWOULDBLOCK on non-blocking sockets.
        // We don't print perror here to avoid cluttering logs with expected
        // "Resource temporarily unavailable" messages.
        return -1;
    }

    if (bytes_read == 0) {
        // Connection has been gracefully closed by the client.
        return 0;
    }

    return bytes_read;
}
