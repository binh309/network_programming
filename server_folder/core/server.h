#ifndef SERVER_H
#define SERVER_H

#include <sys/epoll.h>

#define MAX_EVENTS 64
#define LISTEN_BACKLOG 128
#define SERVER_PORT 8888
#define BUFFER_SIZE 4096

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

#endif
