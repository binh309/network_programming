#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include "packet.h"
#include "network_send.h"
#include "packet_builder.h"

#define BUFFER_SIZE (sizeof(packet_header_t) + MAX_BODY_LEN)

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

    char send_buffer[BUFFER_SIZE];
    size_t serialized_size = 0;

    // Serialize the packet into a buffer
    if (packet_builder_serialize(packet, send_buffer, BUFFER_SIZE, &serialized_size) != 0) {
        fprintf(stderr, "Failed to serialize packet.\n");
        return -1;
    }

    // Send the entire serialized buffer
    if (network_send(sockfd, send_buffer, serialized_size) != 0) {
        fprintf(stderr, "Failed to send complete packet.\n");
        return -1;
    }

    return 0;
}
