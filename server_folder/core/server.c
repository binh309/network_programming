#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include "server.h"
#include "session_manager.h"
#include "../network/packet.h"
#include "../data/account_db.h"
#include "../data/stock_db.h"
#include "../data/portfolio_db.h"
#include "../features/dispatcher.h"
#include "../features/market.h"

// Set socket to non-blocking mode
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Initialize server: create socket, bind, listen, setup epoll
server_context_t* server_init(int port) {
    server_context_t* ctx = malloc(sizeof(server_context_t));
    if (!ctx) {
        perror("malloc");
        return NULL;
    }

    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->listen_fd < 0) {
        perror("socket");
        free(ctx);
        return NULL;
    }

    int opt = 1;
    if (setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(ctx->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    if (listen(ctx->listen_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    if (set_nonblocking(ctx->listen_fd) < 0) {
        perror("fcntl");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (ctx->epoll_fd < 0) {
        perror("epoll_create1");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = ctx->listen_fd;
    if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->listen_fd, &ev) < 0) {
        perror("epoll_ctl");
        close(ctx->epoll_fd);
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    printf("[SERVER] Listening on port %d\n", port);
    return ctx;
}

// Handle incoming client data with buffering
void handle_client_read(server_context_t* ctx, int client_fd) {
    session_t* session = session_mgr_get(client_fd);
    if (!session) return;

    // Read data into the session's buffer
    ssize_t bytes_read = read(client_fd, 
                              session->read_buffer + session->read_offset, 
                              BUFFER_SIZE - session->read_offset);

    if (bytes_read <= 0) {
        if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // No more data to read right now
            return;
        }
        // Connection closed or error
        printf("[SERVER] Client fd=%d disconnected or read error\n", client_fd);
        session_mgr_logout(client_fd);
        close(client_fd);
        epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        return;
    }

    session->read_offset += bytes_read;

    // Process all complete packets in the buffer
    while (session->read_offset >= (int)sizeof(packet_header_t)) {
        packet_header_t* net_header = (packet_header_t*)session->read_buffer;
        uint16_t body_len = ntohs(net_header->length);
        size_t total_packet_size = sizeof(packet_header_t) + body_len;

        if ((size_t)session->read_offset < total_packet_size) {
            // Not enough data for the full packet, wait for more
            break; 
        }

        // We have a full packet
        packet_t request;
        request.header.request_id = ntohs(net_header->request_id);
        request.header.type = net_header->type;
        request.header.length = body_len;

        if (body_len > 0) {
            memcpy(request.body, session->read_buffer + sizeof(packet_header_t), body_len);
        }
        request.body[body_len] = '\0';

        // Dispatch the packet
        dispatcher_handle_message(client_fd, &request, session);
        
        // Remove processed packet from buffer
        int remaining_data = session->read_offset - total_packet_size;
        if (remaining_data > 0) {
            memmove(session->read_buffer, session->read_buffer + total_packet_size, remaining_data);
        }
        session->read_offset = remaining_data;
    }
}


// Main event loop
void server_run(server_context_t* ctx) {
    printf("[SERVER] Starting event loop...\n");

    while (1) {
        int nfds = epoll_wait(ctx->epoll_fd, ctx->events, MAX_EVENTS, -1);
        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = ctx->events[i].data.fd;

            if (fd == ctx->listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(ctx->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }

                if (set_nonblocking(client_fd) < 0) {
                    perror("fcntl client");
                    close(client_fd);
                    continue;
                }
                
                session_mgr_add(client_fd);

                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
                ev.data.fd = client_fd;
                if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                    perror("epoll_ctl add client");
                    close(client_fd);
                    continue;
                }

                printf("[SERVER] New client fd=%d from %s:%d\n", client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            }
            else if (ctx->events[i].events & EPOLLIN) {
                handle_client_read(ctx, fd);
            }
            else if (ctx->events[i].events & (EPOLLERR | EPOLLHUP)) {
                printf("[SERVER] Client fd=%d error/disconnect\n", fd);
                session_mgr_logout(fd);
                close(fd);
                epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            }
        }
    }
}

// Cleanup
void server_shutdown(server_context_t* ctx) {
    if (ctx) {
        if (ctx->epoll_fd >= 0) close(ctx->epoll_fd);
        if (ctx->listen_fd >= 0) close(ctx->listen_fd);
        free(ctx);
    }
    portfolio_db_destroy();
    session_mgr_destroy();
    printf("[SERVER] Shutdown complete\n");
}

// Main entry point
int main(int argc, char* argv[]) {
    int port = SERVER_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    if (!account_db_init(NULL)) {
        fprintf(stderr, "Failed to initialize accounts database\n");
        return 1;
    }

    if (!stock_db_init(NULL)) {
        fprintf(stderr, "Failed to initialize stocks database\n");
        return 1;
    }

    if (!portfolio_db_init()) {
        fprintf(stderr, "Failed to initialize portfolio database\n");
        return 1;
    }
    
    if (session_mgr_init() < 0) {
        fprintf(stderr, "Failed to initialize session manager\n");
        return 1;
    }

    pthread_t market_tid;
    pthread_create(&market_tid, NULL, market_update_thread, NULL);
    pthread_detach(market_tid);

    server_context_t* ctx = server_init(port);
    if (!ctx) {
        return 1;
    }

    server_run(ctx);
    server_shutdown(ctx);

    return 0;
}
