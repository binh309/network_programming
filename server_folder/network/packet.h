#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stddef.h>

#define MAX_BODY_LEN 4096

// Generic Packet Header
typedef struct {
    uint16_t request_id; // Unique ID to match responses to requests
    uint8_t type;        // Message type from protocol.h
    uint16_t length;     // Length of the body
} packet_header_t;

// Generic Packet Structure
typedef struct {
    packet_header_t header;
    char body[MAX_BODY_LEN];
} packet_t;

// Function to create a new packet
void create_packet(packet_t* packet, uint16_t request_id, uint8_t type, const char* body);

// Function to send a packet over a socket
int send_packet(int sockfd, const packet_t* packet);

#endif