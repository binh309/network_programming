#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/socket.h>

#include "protocol.h"
#include "packet.h"

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8888

// --- Globals ---
static int g_socket_fd = -1;
static bool g_is_logged_in = false;
static bool g_is_connected = false;  // Track connection state
static char g_username[32] = {0};
static uint16_t g_request_id = 0;
static char g_server_ip[64] = SERVER_HOST;
static int g_server_port = SERVER_PORT;

// --- Forward Declarations ---
void cmd_register(char* args);
void cmd_login(char* args);
void cmd_logout();
void cmd_view_stocks();
void cmd_my_stocks();
void cmd_buy_stock(char* args);
void cmd_sell_stock(char* args);
void cmd_see_balance();
void cmd_status();
void show_menu();
int attempt_reconnect(void);
void handle_disconnect(void);

// --- Main Functions ---

void handle_response(const packet_t* response) {
    if (response->header.type == SMSG_ERROR) {
        printf("\n✗ ERROR: %s\n\n", response->body);
        return;
    }
    
    // Create a mutable copy for strtok
    char body_copy[MAX_BODY_LEN];
    strncpy(body_copy, response->body, MAX_BODY_LEN);
    body_copy[MAX_BODY_LEN - 1] = '\0';


    switch (response->header.type) {
        case SMSG_REGISTER_SUCCESS:
        case SMSG_LOGIN_SUCCESS:
        case SMSG_BUY_STOCK_SUCCESS:
        case SMSG_SELL_STOCK_SUCCESS:
            printf("\n✓ SUCCESS: %s\n\n", response->body);
            break;
        
        case SMSG_LOGOUT_SUCCESS:
            g_is_logged_in = false;
            memset(g_username, 0, sizeof(g_username));
            printf("\n✓ SUCCESS: %s\n\n", response->body);
            break;

        case SMSG_VIEW_STOCKS_DATA:
            printf("\n===================================== AVAILABLE STOCKS =====================================\n");
            printf("%-4s %-8s %-18s %14s %14s %14s %6s %8s\n", 
                   "ID", "SYMBOL", "NAME", "BID (QTY)", "ASK (QTY)", "LAST (QTY)", "AGE", "SPREAD");
            printf("--------------------------------------------------------------------------------------------\n");
            char* stock_str = strtok(body_copy, ";");
            while (stock_str != NULL) {
                uint16_t id;
                char symbol[16], name[32];
                double bid, ask, last;
                uint32_t bid_qty, ask_qty, last_qty;
                long timestamp;
                
                // NEW FORMAT: ID,SYMBOL,NAME,BID,BID_QTY,ASK,ASK_QTY,LAST,LAST_QTY,TIMESTAMP
                if (sscanf(stock_str, "%hu,%15[^,],%31[^,],%lf,%u,%lf,%u,%lf,%u,%ld", 
                          &id, symbol, name, &bid, &bid_qty, &ask, &ask_qty, &last, &last_qty, &timestamp) == 10) {
                    
                    // Calculate data age
                    time_t now = time(NULL);
                    long data_age = now - timestamp;
                    
                    // Calculate spread
                    double spread = ask - bid;
                    
                    // Format bid/ask/last with qty in parentheses
                    char bid_str[24], ask_str[24], last_str[24];
                    snprintf(bid_str, sizeof(bid_str), "%.2f(%u)", bid, bid_qty);
                    snprintf(ask_str, sizeof(ask_str), "%.2f(%u)", ask, ask_qty);
                    snprintf(last_str, sizeof(last_str), "%.2f(%u)", last, last_qty);
                    
                    printf("%-4u %-8s %-18s %14s %14s %14s %4lds %8.2f\n", 
                           id, symbol, name, bid_str, ask_str, last_str, data_age, spread);
                }
                stock_str = strtok(NULL, ";");
            }
            printf("============================================================================================\n\n");
            break;

        case SMSG_VIEW_MY_STOCKS_DATA:
            printf("\n========== YOUR PORTFOLIO ==========\n");
            printf("%-10s %-10s %-15s %-15s %-10s\n", "SYMBOL", "QTY", "AVG COST", "CURRENT PRICE", "P&L");
            printf("----------------------------------------------------------------\n");
            char* pos_str = strtok(body_copy, ";\n");
            while (pos_str != NULL) {
                char symbol[16];
                uint32_t qty;
                double avg_price, current_price, pnl;
                if (sscanf(pos_str, "%15[^,],%u,%lf,%lf,%lf", symbol, &qty, &avg_price, &current_price, &pnl) == 5) {
                    printf("%-10s %-10u %-15.2f %-15.2f %-10.2f\n", symbol, qty, avg_price, current_price, pnl);
                } else {
                    puts(pos_str);
                }
                pos_str = strtok(NULL, ";\n");
            }
            printf("================================================================\n\n");
            break;
        
        case SMSG_SEE_BALANCE_DATA:
            printf("\n✓ %s\n\n", response->body);
            break;

        default:
            printf("\n[SERVER RESPONSE]: %s\n\n", response->body);
            break;
    }
}

int main(int argc, char* argv[]) {
    // Allow overriding server IP and Port via command line
    if (argc > 1) {
        strncpy(g_server_ip, argv[1], sizeof(g_server_ip) - 1);
    }
    if (argc > 2) {
        g_server_port = atoi(argv[2]);
    }

    g_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_socket_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(g_server_port),
        .sin_addr.s_addr = inet_addr(g_server_ip)
    };

    if (connect(g_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[ERROR] Failed to connect to %s:%d. Check IP and Firewall.\n", g_server_ip, g_server_port);
        perror("connect");
        return 1;
    }

    g_is_connected = true;
    printf("[CLIENT] Connected to server at %s:%d. Welcome to the trading system!\n", g_server_ip, g_server_port);
    show_menu();

    char input[256];
    while (1) {
        // Check if still connected - attempt reconnect if not
        if (!g_is_connected) {
            handle_disconnect();
            if (!attempt_reconnect()) {
                printf("\n[CLIENT] Failed to reconnect. Type 'quit' to exit or any command to retry.\n");
            }
        }
        
        printf(">>> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;

        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0';
        if (strlen(input) == 0) continue;

        char cmd[32] = {0};
        char args[224] = {0};
        sscanf(input, "%31s %223[^\n]", cmd, args);

        if (strcmp(cmd, "register") == 0) cmd_register(args);
        else if (strcmp(cmd, "login") == 0) cmd_login(args);
        else if (strcmp(cmd, "logout") == 0) cmd_logout();
        else if (strcmp(cmd, "stocks") == 0) cmd_view_stocks();
        else if (strcmp(cmd, "my_stocks") == 0) cmd_my_stocks();
        else if (strcmp(cmd, "buy") == 0) cmd_buy_stock(args);
        else if (strcmp(cmd, "sell") == 0) cmd_sell_stock(args);
        else if (strcmp(cmd, "balance") == 0) cmd_see_balance();
        else if (strcmp(cmd, "status") == 0) cmd_status();
        else if (strcmp(cmd, "reconnect") == 0) {
            if (g_is_connected) {
                printf("[CLIENT] Already connected.\n");
            } else {
                handle_disconnect();
                if (attempt_reconnect()) {
                    printf("[CLIENT] Ready to use. Please login.\n");
                } else {
                    printf("[CLIENT] Reconnection failed. Server may be down.\n");
                }
            }
        }
        else if (strcmp(cmd, "help") == 0) show_menu();
        else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            printf("[CLIENT] Goodbye!\n");
            break;
        } else {
            printf("[ERROR] Unknown command: %s\n", cmd);
        }
        fflush(stdout);
    }

    close(g_socket_fd);
    return 0;
}

// --- Command Handlers ---

void cmd_register(char* args) {
    char username[32] = {0};
    char password[32] = {0};
    if (sscanf(args, " %31s %31s", username, password) != 2) {
        printf("Usage: register <username> <password>\n");
        return;
    }

    char body[65];
    snprintf(body, sizeof(body), "%s,%s", username, password); 
    
    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_REGISTER, body);
    send_packet(g_socket_fd, &request);
    
    if (receive_packet(g_socket_fd, &response) == 0) {
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}

void cmd_login(char* args) {
    char username[32] = {0};
    char password[32] = {0};
    if (sscanf(args, " %31s %31s", username, password) != 2) {
        printf("Usage: login <username> <password>\n");
        return;
    }

    printf("[CLIENT] Sending login command\n");
    char body[65];
    snprintf(body, sizeof(body), "%s,%s", username, password);

    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_LOGIN, body);
    printf("[CLIENT] Created packet type=0x%02x, request_id=%u, body=%s\n",
           request.header.type, request.header.request_id, (char*)request.body);
    
    send_packet(g_socket_fd, &request);
    printf("[CLIENT] Login packet sent, waiting for response...\n");

    if (receive_packet(g_socket_fd, &response) == 0) {
        printf("[CLIENT] Received response type=0x%02x\n", response.header.type);
        if (response.header.type == SMSG_LOGIN_SUCCESS) {
            g_is_logged_in = true;
            strncpy(g_username, username, sizeof(g_username) - 1);
            g_username[sizeof(g_username) - 1] = '\0';
        }
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}

void cmd_logout() {
    if (!g_is_logged_in) {
        printf("[ERROR] You are not logged in.\n");
        return;
    }
    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_LOGOUT, NULL);
    send_packet(g_socket_fd, &request);

    if (receive_packet(g_socket_fd, &response) == 0) {
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}


void cmd_view_stocks() {
    if (!g_is_logged_in) {
         printf("[ERROR] You must be logged in to view stocks.\n");
        return;
    }
    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_VIEW_STOCKS, NULL);
    send_packet(g_socket_fd, &request);
    if (receive_packet(g_socket_fd, &response) == 0) {
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}

void cmd_my_stocks() {
    if (!g_is_logged_in) {
         printf("[ERROR] You must be logged in to view your stocks.\n");
        return;
    }
    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_VIEW_MY_STOCKS, NULL);
    send_packet(g_socket_fd, &request);
    if (receive_packet(g_socket_fd, &response) == 0) {
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}

void cmd_buy_stock(char* args) {
     if (!g_is_logged_in) {
        printf("[ERROR] You must be logged in to buy stocks.\n");
        return;
    }
    char type[10] = {0};
    uint16_t id;
    uint32_t qty;
    double price;

    if (sscanf(args, "%hu %u %lf %9s", &id, &qty, &price, type) != 4) {
        printf("Usage: buy <stock_id> <quantity> <price> <type|MARKET|LIMIT>\n");
        return;
    }

    char body[256];
    snprintf(body, sizeof(body), "%hu,%u,%f,%s", id, qty, price, type); 
    
    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_BUY_STOCK, body);
    send_packet(g_socket_fd, &request);
    if (receive_packet(g_socket_fd, &response) == 0) {
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}

void cmd_sell_stock(char* args) {
    if (!g_is_logged_in) {
        printf("[ERROR] You must be logged in to sell stocks.\n");
        return;
    }
    char type[10] = {0};
    uint16_t id;
    uint32_t qty;
    double price;

    if (sscanf(args, "%hu %u %lf %9s", &id, &qty, &price, type) != 4) {
        printf("Usage: sell <stock_id> <quantity> <price> <type|MARKET|LIMIT>\n");
        return;
    }

    char body[256];
    snprintf(body, sizeof(body), "%hu,%u,%f,%s", id, qty, price, type);

    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_SELL_STOCK, body);
    send_packet(g_socket_fd, &request);

    if (receive_packet(g_socket_fd, &response) == 0) {
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}

void cmd_see_balance() {
    if (!g_is_logged_in) {
        printf("[ERROR] You must be logged in to see your balance.\n");
        return;
    }
    packet_t request, response;
    create_packet(&request, ++g_request_id, CMSG_SEE_BALANCE, NULL);
    send_packet(g_socket_fd, &request);
    if (receive_packet(g_socket_fd, &response) == 0) {
        handle_response(&response);
    } else {
        g_is_connected = false;
    }
}

void cmd_status(void) {
    // Active connection check: try to peek at socket
    if (g_is_connected && g_socket_fd >= 0) {
        char buf;
        int result = recv(g_socket_fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
        if (result == 0) {
            // Connection closed by server
            g_is_connected = false;
            g_is_logged_in = false;
            printf("\n[WARNING] Server has closed the connection.\n");
        }
        // result < 0 with EAGAIN/EWOULDBLOCK is OK (no data, still connected)
        // result < 0 with other errors might indicate problems
        else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            g_is_connected = false;
            g_is_logged_in = false;
            printf("\n[WARNING] Connection error detected.\n");
        }
    }
    
    printf("\n========== CLIENT STATUS ==========\n");
    printf("  Connected to server: %s\n", g_is_connected ? "YES" : "NO (DISCONNECTED)");
    printf("  Logged in:           %s\n", g_is_logged_in ? "YES" : "NO");
    if (g_is_logged_in) {
        printf("  Username:            %s\n", g_username);
    }
    printf("===================================\n\n");
}

void show_menu(void) {
    printf("\n========== TRADING CLIENT MENU ==========\n");
    printf("  register <user> <pass>         - Create a new account\n");
    printf("  login <user> <pass>            - Login to your account\n");
    printf("  logout                         - Logout from your account\n");
    printf("  stocks                         - View available stocks\n");
    printf("  my_stocks                      - View your portfolio\n");
    printf("  buy <id> <qty> <price> <type>  - Buy stock (type: MARKET or LIMIT)\n");
    printf("  sell <id> <qty> <price> <type> - Sell stock (type: MARKET or LIMIT)\n");
    printf("  balance                        - Check your account balance\n");
    printf("  status                         - Show your current connection status\n");
    printf("  reconnect                      - Manually reconnect to server\n");
    printf("  help                           - Show this menu\n");
    printf("  quit                           - Exit the client\n");
    printf("========================================\n\n");
}

// --- Connection Management ---

void handle_disconnect(void) {
    // Clear login state on disconnect
    if (g_is_logged_in) {
        printf("\n[WARNING] Connection lost - you have been logged out.\n");
        g_is_logged_in = false;
        memset(g_username, 0, sizeof(g_username));
    }
    
    // Close old socket
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
        g_socket_fd = -1;
    }
}

int attempt_reconnect(void) {
    printf("[CLIENT] Attempting to reconnect to %s:%d...\n", g_server_ip, g_server_port);
    
    // Create new socket
    g_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_socket_fd < 0) {
        perror("socket");
        return 0;
    }
    
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(g_server_port),
        .sin_addr.s_addr = inet_addr(g_server_ip)
    };
    
    if (connect(g_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(g_socket_fd);
        g_socket_fd = -1;
        return 0;
    }
    
    g_is_connected = true;
    printf("[CLIENT] Reconnected successfully! Please login again.\n");
    return 1;
}
