#include "ui/tui.h"
#include "socket_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>  // For TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT

/**
 * @brief Set a file descriptor to non-blocking mode.
 * 
 * @param fd The file descriptor.
 * @return 0 on success, -1 on error.
 */
int socket_io_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return -1;
    }
    return 0;
}

/**
 * @brief Creates, binds, and listens on a server socket.
 * 
 * @param port The port to listen on.
 * @return The listening socket file descriptor, or -1 on error.
 */
int socket_io_create_server(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return -1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    if (socket_io_set_nonblocking(listen_fd) < 0) {
        perror("socket_io_set_nonblocking");
        close(listen_fd);
        return -1;
    }

    server_debug("[SocketIO] Server listening on port %d\n", port);
    return listen_fd;
}

/**
 * @brief Enable TCP Keep-Alive on a socket
 * 
 * Configures the socket to detect dead connections:
 * - Start probing after 10 seconds of inactivity
 * - Send probes every 5 seconds
 * - Close connection after 3 failed probes
 * 
 * @param sockfd The socket to configure
 * @return 0 on success, -1 on error
 */
int socket_io_enable_keepalive(int sockfd) {
    int optval = 1;
    
    // Enable keep-alive
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_KEEPALIVE");
        return -1;
    }
    
    // Start probing after 10 seconds of inactivity
    optval = 10;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) < 0) {
        perror("setsockopt TCP_KEEPIDLE");
        return -1;
    }
    
    // Send probe every 5 seconds
    optval = 5;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) < 0) {
        perror("setsockopt TCP_KEEPINTVL");
        return -1;
    }
    
    // Close after 3 failed probes (10 + 5*3 = 25 seconds max to detect dead connection)
    optval = 3;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval)) < 0) {
        perror("setsockopt TCP_KEEPCNT");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Accepts a new client connection.
 * 
 * @param listen_fd The listening socket.
 * @param client_addr Pointer to a sockaddr_in struct to hold client address info.
 * @return The client socket file descriptor, or -1 on error.
 */
int socket_io_accept_connection(int listen_fd, struct sockaddr_in* client_addr) {
    socklen_t addr_len = sizeof(*client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr*)client_addr, &addr_len);
    if (client_fd < 0) {
        // Errors are expected on non-blocking sockets, so don't print them all.
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept");
        }
        return -1;
    }
    
    // Enable TCP Keep-Alive for connection monitoring
    socket_io_enable_keepalive(client_fd);
    
    return client_fd;
}

/**
 * @brief Closes a socket file descriptor.
 * 
 * @param fd The file descriptor to close.
 */
void socket_io_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}
