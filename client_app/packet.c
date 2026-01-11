#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include "packet.h"

// Helper function to send a precise number of bytes
static int send_all(int sockfd, const void* buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = write(sockfd, (const char*)buf + total_sent, len - total_sent);
        if (sent < 0) {
            // On non-blocking sockets, EAGAIN and EWOULDBLOCK are expected.
            // For this simple client, we'll treat them as errors.
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                perror("write would block");
                return -1;
            }
            perror("write failed in send_all");
            return -1;
        }
        if (sent == 0) {
            fprintf(stderr, "Connection closed by peer during send.\n");
            return -1;
        }
        total_sent += sent;
    }
    return 0;
}

// Helper function to receive a precise number of bytes
static int recv_all(int sockfd, void* buf, size_t len) {
    size_t total_recv = 0;
    while (total_recv < len) {
        ssize_t received = read(sockfd, (char*)buf + total_recv, len - total_recv);
        if (received < 0) {
             if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // This shouldn't happen with a blocking read, but as a safeguard...
                perror("read would block");
                return -1;
            }
            perror("read failed in recv_all");
            return -1;
        }
        if (received == 0) {
            fprintf(stderr, "Connection closed by peer during receive.\n");
            return -1; 
        }
        total_recv += received;
    }
    return 0;
}


// Create a new packet
void create_packet(packet_t* packet, uint16_t request_id, uint8_t type, const char* body) {
    if (!packet) return;

    packet->header.request_id = request_id;
    packet->header.type = type;
    
    if (body) {
        size_t body_len = strlen(body);
        if (body_len >= MAX_BODY_LEN) {
            body_len = MAX_BODY_LEN - 1;
            fprintf(stderr, "[PACKET] Warning: Body truncated to %ld bytes.\n", body_len);
        }
        packet->header.length = (uint16_t)body_len;
        memcpy(packet->body, body, body_len);
        packet->body[body_len] = '\0';
    } else {
        packet->header.length = 0;
        packet->body[0] = '\0';
    }
}

// Send a packet over a socket
int send_packet(int sockfd, const packet_t* packet) {
    if (!packet) return -1;

    // Prepare header for network transmission
    packet_header_t net_header;
    net_header.request_id = htons(packet->header.request_id);
    net_header.type = packet->header.type;
    net_header.length = htons(packet->header.length);

    // Send header
    if (send_all(sockfd, &net_header, sizeof(packet_header_t)) != 0) {
        fprintf(stderr, "Failed to send packet header.\n");
        return -1;
    }

    // Send body
    if (packet->header.length > 0) {
        if (send_all(sockfd, packet->body, packet->header.length) != 0) {
            fprintf(stderr, "Failed to send packet body.\n");
            return -1;
        }
    }

    return 0;
}


// Receive a packet from a socket
int receive_packet(int sockfd, packet_t* packet) {
    if (!packet) return -1;

    // Read the header first
    packet_header_t net_header;
    if (recv_all(sockfd, &net_header, sizeof(packet_header_t)) != 0) {
        // Error message is printed in recv_all
        return -1; 
    }

    // Convert header from network to host byte order
    packet->header.request_id = ntohs(net_header.request_id);
    packet->header.type = net_header.type;
    packet->header.length = ntohs(net_header.length);

    if (packet->header.length >= MAX_BODY_LEN) {
        fprintf(stderr, "[PACKET] Declared body length (%u) exceeds or meets max (%d).\n", packet->header.length, MAX_BODY_LEN);
        return -1;
    }

    // Read the body
    if (packet->header.length > 0) {
        if (recv_all(sockfd, packet->body, packet->header.length) != 0) {
            fprintf(stderr, "Failed to receive packet body.\n");
            return -1;
        }
    }
    
    // Null-terminate the body for safety
    packet->body[packet->header.length] = '\0';

    return 0;
}