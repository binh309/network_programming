#include <stdio.h>
#include <string.h>
#include "login.h"
#include "../data/account_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../core/connection_manager.h"
#include "../model/error.h"

// Handle login request
void handle_login_request(int client_socket, const packet_t* request, connection_t* connection) {
    printf("[LOGIN_HANDLER] Called on fd=%d\n", client_socket);
    
    if (connection->is_logged_in) {
        printf("[LOGIN_HANDLER] Already logged in\n");
        send_error(client_socket, request->header.request_id, "Already logged in.");
        return;
    }

    // Body should be in "username,password" format
    char username[32] = {0};
    char password[32] = {0};

    printf("[LOGIN_HANDLER] Parsing body: %.50s\n", (char*)request->body);
    
    if (sscanf(request->body, "%31[^,],%31s", username, password) != 2) {
        printf("[LOGIN_HANDLER] Parse failed\n");
        send_error(client_socket, request->header.request_id, "Invalid login format. Use: USERNAME,PASSWORD");
        return;
    }

    printf("[LOGIN_HANDLER] Parsed username=%s, password=%s\n", username, password);

    // Lookup user in the database
    account_t* acc = account_db_lookup(username, password);

    if (acc) {
        // Login successful
        printf("[LOGIN_HANDLER] ✓ Authentication successful for user %s (ID=%u)\n", 
               acc->username, acc->user_id);
        
        connection->is_logged_in = true;
        connection->user_id = acc->user_id;
        strncpy(connection->username, acc->username, sizeof(connection->username) - 1);
        connection->username[sizeof(connection->username) - 1] = '\0';
        
        printf("[AUTH] User '%s' (ID: %u) logged in from socket %d\n", acc->username, acc->user_id, client_socket);

        packet_t response;
        char response_body[128];
        snprintf(response_body, sizeof(response_body), "Login successful. Welcome, %s!", acc->username);
        printf("[LOGIN_HANDLER] → Sending success response\n");
        create_packet(&response, request->header.request_id, SMSG_LOGIN_SUCCESS, response_body);
        send_packet(client_socket, &response);
        printf("[LOGIN_HANDLER] Success response sent\n");

        account_db_free(acc); // Free the copied account struct
    } else {
        // Login failed
        printf("[LOGIN_HANDLER] ✗ Authentication failed for user %s\n", username);
        send_error(client_socket, request->header.request_id, "Invalid username or password.");
    }
}
