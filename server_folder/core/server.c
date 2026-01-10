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
#include "portfolio_manager.h"
#include "../network/protocol.h"
#include "../data/account_db.h"
#include "../data/stock_db.h"
#include "../data/transaction_db.h"
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

    // Create listening socket
    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->listen_fd < 0) {
        perror("socket");
        free(ctx);
        return NULL;
    }

    // Allow socket reuse
    int opt = 1;
    if (setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    // Bind to port
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

    // Listen for connections
    if (listen(ctx->listen_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    // Set to non-blocking
    if (set_nonblocking(ctx->listen_fd) < 0) {
        perror("fcntl");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    // Create epoll instance
    ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (ctx->epoll_fd < 0) {
        perror("epoll_create1");
        close(ctx->listen_fd);
        free(ctx);
        return NULL;
    }

    // Register listening socket with epoll
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

// Handle incoming client data
void handle_client_read(server_context_t* ctx, int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t n = recv(client_fd, buffer, BUFFER_SIZE, 0);

    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recv");
            close(client_fd);
            epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        }
        return;
    }

    if (n == 0) {
        // Client disconnected
        printf("[SERVER] Client fd=%d disconnected\n", client_fd);
        session_mgr_logout(client_fd);
        close(client_fd);
        epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        return;
    }

    // Parse packet header
    size_t bytes_received = (size_t)n;
    if (bytes_received >= sizeof(struct packet_header)) {
        struct packet_header* hdr = (struct packet_header*)buffer;
        printf("[SERVER] Received message type=0x%02x, length=%u from fd=%d\n", hdr->type, hdr->length, client_fd);

        // Check if we have the full message (header + payload)
        if (bytes_received >= sizeof(struct packet_header) + hdr->length) {
            char* payload = buffer + sizeof(struct packet_header);
            dispatcher_handle_message(client_fd, hdr, payload);
        }
    }
}

// Handle login request - validate against accounts database
void handle_login_request(int client_fd, struct login_payload* payload) {
    printf("[SERVER] Login request: username=%s\n", payload->username);

    // Lookup account in database
    account_t* acc = account_db_lookup(payload->username, payload->password);

    struct packet_header resp_hdr = {
        .type = MSG_LOGIN_RESPONSE,
        .length = sizeof(struct login_response)
    };

    struct login_response resp;

    if (acc && acc->found) {
        resp.status = STATUS_SUCCESS;
        snprintf(resp.message, 64, "Login successful! Balance: $%.2f", acc->balance);
        printf("[SERVER] User %s authenticated (ID=%u, Balance=%.2f)\n", acc->username, acc->user_id, acc->balance);
        
        // Register session
        session_mgr_authenticate(client_fd, acc->user_id, acc->username);
    } else {
        resp.status = STATUS_FAILED;
        strncpy(resp.message, "Invalid username or password", 64);
        printf("[SERVER] Authentication failed for user %s\n", payload->username);
    }

    // Send header + payload together
    char buffer[sizeof(struct packet_header) + sizeof(struct login_response)];
    memcpy(buffer, &resp_hdr, sizeof(resp_hdr));
    memcpy(buffer + sizeof(resp_hdr), &resp, sizeof(resp));
    send(client_fd, buffer, sizeof(buffer), 0);

    printf("[SERVER] Sent login response to fd=%d (status=%d)\n", client_fd, resp.status);

    if (acc) {
        account_db_free(acc);
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

            // New connection on listening socket
            if (fd == ctx->listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                int client_fd = accept(ctx->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }

                // Set client socket to non-blocking
                if (set_nonblocking(client_fd) < 0) {
                    perror("fcntl client");
                    close(client_fd);
                    continue;
                }

                // Add client to epoll
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
            // Data ready on client socket
            else if (ctx->events[i].events & EPOLLIN) {
                handle_client_read(ctx, fd);
            }
            // Error or client disconnection
            else if (ctx->events[i].events & (EPOLLERR | EPOLLHUP)) {
                printf("[SERVER] Client fd=%d error/disconnect\n", fd);
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
    printf("[SERVER] Shutdown complete\n");
}

// Main entry point
int main(int argc, char* argv[]) {
    int port = SERVER_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    // Initialize accounts database
    if (account_db_init(NULL) < 0) {
        fprintf(stderr, "Failed to initialize accounts database\n");
        return 1;
    }

    // Initialize stocks database
    if (stock_db_init(NULL) <= 0) {
        fprintf(stderr, "Failed to initialize stocks database\n");
        return 1;
    }

    // Initialize session manager
    if (session_mgr_init() < 0) {
        fprintf(stderr, "Failed to initialize session manager\n");
        return 1;
    }

    // Initialize portfolio manager
    if (portfolio_mgr_init() < 0) {
        fprintf(stderr, "Failed to initialize portfolio manager\n");
        return 1;
    }

    // Initialize transaction database
    if (transaction_db_init() < 0) {
        fprintf(stderr, "Failed to initialize transaction database\n");
        return 1;
    }

    // Start market update thread
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
