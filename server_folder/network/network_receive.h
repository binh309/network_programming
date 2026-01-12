#ifndef NETWORK_RECEIVE_H
#define NETWORK_RECEIVE_H

#include <sys/types.h>

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
 *         or -1 on error. If -1 is returned, errno will be set.
 */
ssize_t network_receive(int sockfd, void* buffer, size_t buffer_size);

#endif // NETWORK_RECEIVE_H
