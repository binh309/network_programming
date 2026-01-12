#include <stdio.h>
#include <string.h>
#include "register.h"
#include "../data/account_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

// Handle registration request
void handle_register_request(int client_socket, const packet_t* request, connection_t* connection) {
    if (connection->is_logged_in) {
        send_error(client_socket, request->header.request_id, "Cannot register while logged in.");
        return;
    }

    // Body should be in "username,password" format
    char username[32] = {0};
    char password[32] = {0};

    if (sscanf(request->body, "%31[^,],%31s", username, password) != 2) {
        send_error(client_socket, request->header.request_id, "Invalid registration format. Use: USERNAME,PASSWORD");
        return;
    }

    // Check if username already exists
    if (account_db_username_exists(username)) {
        send_error(client_socket, request->header.request_id, "Username already taken.");
        return;
    }

    // Attempt to add the new user
    if (account_db_add(username, password)) {
        // On success, create a response packet
        packet_t response;
        create_packet(&response, request->header.request_id, CMSG_REGISTER, "Registration successful. Please log in.");
        send_packet(client_socket, &response);
    } else {
        send_error(client_socket, request->header.request_id, "Registration failed. Server error.");
    }
}
