#ifndef SOCKET_IO_H
#define SOCKET_IO_H

#include <netinet/in.h>

int socket_io_create_server(int port);
int socket_io_accept_connection(int listen_fd, struct sockaddr_in* client_addr);
int socket_io_set_nonblocking(int fd);
void socket_io_close(int fd);

#endif // SOCKET_IO_H
