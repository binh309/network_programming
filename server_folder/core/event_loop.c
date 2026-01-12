#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include "event_loop.h"
#include "connection_manager.h"
#include "request_handler.h"
#include "../network/socket_io.h"

#define MAX_EVENTS 100
#define EPOLL_TIMEOUT 1000  // 1 second

/**
 * @brief Initialize the event loop
 *
 * Creates:
 * 1. Server socket (Layer 1: socket_io)
 * 2. Epoll multiplexer
 * 3. Adds listening socket to epoll
 *
 * Returns context for later use in event_loop_run()
 */
event_loop_ctx_t* event_loop_init(int port) {
    printf("[EVENT_LOOP] Initializing on port %d\n", port);

    // Layer 1: Create server socket
    int listen_fd = socket_io_create_server(port);
    if (listen_fd < 0) {
        fprintf(stderr, "[EVENT_LOOP] Failed to create server socket\n");
        return NULL;
    }

    // Create epoll
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("[EVENT_LOOP] epoll_create1");
        close(listen_fd);
        return NULL;
    }

    // Register listening socket for accept events
    struct epoll_event ev;
    ev.events = EPOLLIN;           // Ready to accept
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        perror("[EVENT_LOOP] epoll_ctl (add listen_fd)");
        close(epoll_fd);
        close(listen_fd);
        return NULL;
    }

    // Allocate context
    event_loop_ctx_t* loop = malloc(sizeof(event_loop_ctx_t));
    if (!loop) {
        fprintf(stderr, "[EVENT_LOOP] Failed to allocate event loop context\n");
        close(epoll_fd);
        close(listen_fd);
        return NULL;
    }

    loop->epoll_fd = epoll_fd;
    loop->listen_fd = listen_fd;
    loop->port = port;
    loop->running = 0;

    printf("[EVENT_LOOP] Initialized successfully\n");
    return loop;
}

/**
 * @brief Main event loop
 *
 * Infinite loop:
 * 1. epoll_wait() for events
 * 2. For each event:
 *    - If listen_fd: accept new connection (Layer 1: socket_io)
 *    - If client_fd: process request (Layer 3: request_handler)
 *
 * Very thin - only coordinates layers below it
 */
void event_loop_run(event_loop_ctx_t* loop) {
    if (!loop) {
        fprintf(stderr, "[EVENT_LOOP] NULL loop context\n");
        return;
    }

    printf("[EVENT_LOOP] Starting event loop\n");
    loop->running = 1;

    struct epoll_event events[MAX_EVENTS];

    while (loop->running) {
        // Wait for events
        int nfds = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, EPOLL_TIMEOUT);

        if (nfds < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, continue
                continue;
            }
            perror("[EVENT_LOOP] epoll_wait");
            break;
        }

        // Process all events
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            // Check if this is the listening socket
            if (fd == loop->listen_fd) {
                // ===== NEW CONNECTION =====
                printf("[EVENT_LOOP] New connection attempt on listening socket\n");

                struct sockaddr_in client_addr;
                // Layer 1: Accept connection
                int client_fd = socket_io_accept_connection(loop->listen_fd, &client_addr);
                if (client_fd < 0) {
                    fprintf(stderr, "[EVENT_LOOP] Failed to accept connection\n");
                    continue;
                }

                printf("[EVENT_LOOP] Accepted client on fd %d\n", client_fd);

                // Add to connection manager (Layer 3)
                connection_t* conn = connection_mgr_add(client_fd);
                if (!conn) {
                    fprintf(stderr, "[EVENT_LOOP] Failed to register connection\n");
                    close(client_fd);
                    continue;
                }

                // Transition to READY state
                if (connection_mgr_set_state(conn, CONN_READY) < 0) {
                    fprintf(stderr, "[EVENT_LOOP] Failed to set connection state\n");
                    connection_mgr_remove(client_fd);
                    close(client_fd);
                    continue;
                }

                // Register client socket for read events
                struct epoll_event client_ev;
                client_ev.events = EPOLLIN | EPOLLRDHUP;  // Data available or client close
                client_ev.data.fd = client_fd;
                if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                    perror("[EVENT_LOOP] epoll_ctl (add client_fd)");
                    connection_mgr_remove(client_fd);
                    close(client_fd);
                    continue;
                }

            } else {
                // ===== CLIENT SOCKET EVENT =====
                printf("[EVENT_LOOP] Event on client fd %d\n", fd);

                // First, get the connection and check its state
                connection_t* conn = connection_mgr_get(fd);
                if (!conn) {
                    printf("[EVENT_LOOP] Connection not found for fd %d (already removed)\n", fd);
                    epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                // Check for idle connections (timeout after 5 minutes)
                #define IDLE_TIMEOUT_SECONDS (5 * 60)  // 5 minutes
                if (connection_mgr_is_idle(conn, IDLE_TIMEOUT_SECONDS)) {
                    printf("[EVENT_LOOP] ⏱ Closing idle connection on fd %d (no activity for %d seconds)\n", 
                           fd, IDLE_TIMEOUT_SECONDS);
                    epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    connection_mgr_remove(fd);
                    close(fd);
                    continue;
                }

                // Check for disconnect events
                if (events[i].events & EPOLLRDHUP) {
                    printf("[EVENT_LOOP] Client on fd %d closed connection\n", fd);
                    epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    connection_mgr_remove(fd);
                    close(fd);
                    continue;
                }

                // VALIDATION: Check if connection is in a valid state for processing
                // Only CONN_READY, CONN_ACCEPTING should be processed
                // Skip CONN_CLOSED, CONN_CLOSING, CONN_PROCESSING
                if (!connection_mgr_is_valid_for_processing(conn)) {
                    ConnectionState state = connection_mgr_get_state(conn);
                    printf("[EVENT_LOOP] ✗ Skipping event on fd %d - invalid state: %s (%d)\n", 
                           fd, connection_mgr_state_name(state), state);
                    
                    // If it's in CLOSING or CLOSED state, clean it up
                    if (state == CONN_CLOSING || state == CONN_CLOSED) {
                        epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        connection_mgr_remove(fd);
                        close(fd);
                    }
                    continue;
                }
                
                ConnectionState cur_state = connection_mgr_get_state(conn);
                printf("[EVENT_LOOP] ✓ Valid state for processing: %s (%d)\n", 
                       connection_mgr_state_name(cur_state), cur_state);

                // Process incoming request (Layer 3: request_handler)
                int result = request_handler_process(fd);
                if (result < 0) {
                    // Connection should be closed
                    printf("[EVENT_LOOP] Closing connection on fd %d\n", fd);
                    epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    connection_mgr_remove(fd);
                    close(fd);
                }
            }
        }
    }

    printf("[EVENT_LOOP] Event loop stopped\n");
}

/**
 * @brief Shutdown the event loop
 *
 * Closes all sockets and frees memory
 */
void event_loop_shutdown(event_loop_ctx_t* loop) {
    if (!loop) return;

    printf("[EVENT_LOOP] Shutting down\n");
    loop->running = 0;

    if (loop->listen_fd >= 0) {
        close(loop->listen_fd);
    }

    if (loop->epoll_fd >= 0) {
        close(loop->epoll_fd);
    }

    free(loop);
    printf("[EVENT_LOOP] Shutdown complete\n");
}
