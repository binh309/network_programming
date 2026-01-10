#ifndef SERVER_H
#define SERVER_H

#include <sys/epoll.h>
#include "../network/protocol.h"

#define MAX_EVENTS 64
#define LISTEN_BACKLOG 128
#define SERVER_PORT 8888
#define BUFFER_SIZE 1024

// Client connection context
typedef struct {
    int fd;
    char read_buffer[BUFFER_SIZE];
    int read_offset;
    uint8_t state;  // 0 = waiting for header, 1 = waiting for payload
} client_context_t;

// Server context
typedef struct {
    int listen_fd;
    int epoll_fd;
    struct epoll_event events[MAX_EVENTS];
} server_context_t;

// Function declarations
server_context_t* server_init(int port);
void server_run(server_context_t* ctx);
void server_shutdown(server_context_t* ctx);
void handle_client_read(server_context_t* ctx, int client_fd);
void handle_login_request(int client_fd, struct login_payload* payload);

#endif
