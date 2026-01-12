#ifndef NETWORK_SEND_H
#define NETWORK_SEND_H

#include <sys/types.h>

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
int network_send(int sockfd, const void* buffer, size_t length);

#endif // NETWORK_SEND_H
