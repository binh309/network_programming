#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../server_folder/network/protocol.h"

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8888

typedef struct {
    int socket_fd;
    int authenticated;
    char username[32];
    pthread_mutex_t lock;
} client_session_t;

client_session_t session = {0};

void* receive_thread_func(void* arg);
void cmd_view_stocks(void);
void cmd_login(char* args);
void cmd_buy_stock(char* args);
void cmd_sell_stock(char* args);
void cmd_see_balance(void);
void cmd_status(void);
void show_menu(void);

int recv_exact(int sock, void* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(sock, (char*)buf + received, len - received, 0);
        if (n <= 0) return -1;
        received += n;
    }
    return 0;
}

void display_market_update(char* buffer, int buffer_len __attribute__((unused))) {
    struct view_stocks_response* resp = (struct view_stocks_response*)buffer;
    printf("\n\n╔════════════ MARKET UPDATE ════════════╗\n");
    printf("║ Stock Count: %-24u ║\n", resp->stock_count);
    printf("╚═══════════════════════════════════════╝\n");
    struct stock_info* stocks = (struct stock_info*)(buffer + sizeof(struct view_stocks_response));
    for (int i = 0; i < resp->stock_count; i++) {
        printf("  [%u] %s - Price: $%.2f | Available: %u shares\n",
               stocks[i].stock_id, stocks[i].symbol, stocks[i].current_price, stocks[i].available_quantity);
    }
    printf("═════════════════════════════════════════\n\n");
    fflush(stdout);
    printf(">>> ");
    fflush(stdout);
}

void* receive_thread_func(void* arg __attribute__((unused))) {
    // Receive thread is now disabled - use on-demand updates instead
    // This prevents interrupting the user's input with unsolicited market updates
    return NULL;
}

void cmd_login(char* args) {
    char username[32] = {0};
    char password[32] = {0};
    if (sscanf(args, "%31s %31s", username, password) != 2) {
        printf("Usage: login <username> <password>\n");
        fflush(stdout);
        return;
    }
    struct packet_header head = {.type = MSG_LOGIN_REQUEST, .length = sizeof(struct login_payload)};
    struct login_payload payload;
    memset(&payload, 0, sizeof(payload));
    snprintf(payload.username, sizeof(payload.username), "%s", username);
    snprintf(payload.password, sizeof(payload.password), "%s", password);
    send(session.socket_fd, &head, sizeof(head), 0);
    send(session.socket_fd, &payload, sizeof(payload), 0);
    printf("[CLIENT] Sent login request\n");
    fflush(stdout);
    
    struct packet_header resp_head;
    if (recv_exact(session.socket_fd, &resp_head, sizeof(resp_head)) < 0) {
        printf("[ERROR] Failed to receive login response header\n");
        fflush(stdout);
        return;
    }
    
    if (resp_head.type == MSG_LOGIN_RESPONSE && resp_head.length > 0) {
        struct login_response resp;
        memset(&resp, 0, sizeof(resp));
        if (recv_exact(session.socket_fd, &resp, resp_head.length) < 0) {
            printf("[ERROR] Failed to receive login response body\n");
            fflush(stdout);
            return;
        }
        if (resp.status == STATUS_SUCCESS) {
            pthread_mutex_lock(&session.lock);
            session.authenticated = 1;
            snprintf(session.username, sizeof(session.username), "%s", username);
            pthread_mutex_unlock(&session.lock);
            printf("\n╔════════════ LOGIN SUCCESS ════════════╗\n");
            printf("║ %s\n", resp.message);
            printf("╚═══════════════════════════════════════╝\n");
            printf("[INFO] You are now logged in. Type 'stocks' to view available stocks.\n\n");
        } else {
            printf("\n✗ LOGIN FAILED: %s\n", resp.message);
            printf("   Please try again with correct credentials.\n\n");
        }
    } else {
        printf("[ERROR] Invalid login response (type=0x%02x, len=%u)\n", resp_head.type, resp_head.length);
    }
    fflush(stdout);
}

void cmd_view_stocks(void) {
    if (!session.authenticated) {
        printf("[ERROR] Must login first!\n");
        return;
    }
    struct packet_header head = {.type = MSG_VIEW_STOCKS_REQUEST, .length = 0};
    send(session.socket_fd, &head, sizeof(head), 0);
    struct packet_header resp_head;
    if (recv_exact(session.socket_fd, &resp_head, sizeof(resp_head)) < 0) return;
    if (resp_head.type == MSG_VIEW_STOCKS_RESPONSE) {
        char* buffer = malloc(resp_head.length);
        if (!buffer) return;
        if (recv_exact(session.socket_fd, buffer, resp_head.length) < 0) {
            free(buffer);
            return;
        }
        struct view_stocks_response* resp = (struct view_stocks_response*)buffer;
        printf("\n========== AVAILABLE STOCKS ==========\n");
        printf("Stock Count: %u\n\n", resp->stock_count);
        struct stock_info* stocks = (struct stock_info*)(buffer + sizeof(struct view_stocks_response));
        for (int i = 0; i < resp->stock_count; i++) {
            printf("  [%u] %s - Price: $%.2f | Available: %u shares\n",
                   stocks[i].stock_id, stocks[i].symbol, stocks[i].current_price, stocks[i].available_quantity);
        }
        printf("=====================================\n\n");
        free(buffer);
    }
}

void cmd_buy_stock(char* args) {
    if (!session.authenticated) {
        printf("[ERROR] Must login first!\n");
        fflush(stdout);
        return;
    }

    uint16_t stock_id = 0;
    uint32_t quantity = 0;
    double price = 0.0;

    if (sscanf(args, "%hu %u %lf", &stock_id, &quantity, &price) != 3) {
        printf("Usage: buy <stock_id> <quantity> <price>\n");
        printf("Example: buy 1 100 150.25\n");
        fflush(stdout);
        return;
    }

    struct packet_header head = {
        .type = MSG_BUY_STOCK_REQUEST,
        .length = sizeof(struct buy_stock_request)
    };

    struct buy_stock_request payload;
    memset(&payload, 0, sizeof(payload));
    payload.stock_id = stock_id;
    payload.quantity = quantity;
    payload.price_per_unit = price;

    // Send header + payload together
    char buffer[sizeof(struct packet_header) + sizeof(struct buy_stock_request)];
    memcpy(buffer, &head, sizeof(head));
    memcpy(buffer + sizeof(head), &payload, sizeof(payload));
    send(session.socket_fd, buffer, sizeof(buffer), 0);

    struct packet_header resp_head;
    if (recv_exact(session.socket_fd, &resp_head, sizeof(resp_head)) < 0) {
        printf("[ERROR] Failed to receive response\n");
        fflush(stdout);
        return;
    }

    if (resp_head.type == MSG_BUY_STOCK_RESPONSE) {
        struct buy_stock_response resp;
        memset(&resp, 0, sizeof(resp));
        if (recv_exact(session.socket_fd, &resp, resp_head.length) < 0) {
            printf("[ERROR] Failed to receive response body\n");
            fflush(stdout);
            return;
        }

        if (resp.status == 0) {  // STATUS_SUCCESS
            printf("\n✓ SUCCESS: %s\n\n", resp.message);
        } else {
            printf("\n✗ ERROR: %s\n\n", resp.message);
        }
    }
    fflush(stdout);
}

void cmd_sell_stock(char* args) {
    if (!session.authenticated) {
        printf("[ERROR] Must login first!\n");
        fflush(stdout);
        return;
    }

    uint16_t stock_id = 0;
    uint32_t quantity = 0;
    double price = 0.0;

    if (sscanf(args, "%hu %u %lf", &stock_id, &quantity, &price) != 3) {
        printf("Usage: sell <stock_id> <quantity> <price>\n");
        printf("Example: sell 1 50 155.00\n");
        fflush(stdout);
        return;
    }

    struct packet_header head = {
        .type = MSG_SELL_STOCK_REQUEST,
        .length = sizeof(struct sell_stock_request)
    };

    struct sell_stock_request payload;
    memset(&payload, 0, sizeof(payload));
    payload.stock_id = stock_id;
    payload.quantity = quantity;
    payload.price_per_unit = price;

    // Send header + payload together
    char buffer[sizeof(struct packet_header) + sizeof(struct sell_stock_request)];
    memcpy(buffer, &head, sizeof(head));
    memcpy(buffer + sizeof(head), &payload, sizeof(payload));
    send(session.socket_fd, buffer, sizeof(buffer), 0);

    struct packet_header resp_head;
    if (recv_exact(session.socket_fd, &resp_head, sizeof(resp_head)) < 0) {
        printf("[ERROR] Failed to receive response\n");
        fflush(stdout);
        return;
    }

    if (resp_head.type == MSG_SELL_STOCK_RESPONSE) {
        struct sell_stock_response resp;
        memset(&resp, 0, sizeof(resp));
        if (recv_exact(session.socket_fd, &resp, resp_head.length) < 0) {
            printf("[ERROR] Failed to receive response body\n");
            fflush(stdout);
            return;
        }

        if (resp.status == 0) {  // STATUS_SUCCESS
            printf("\n✓ SUCCESS: %s\n\n", resp.message);
        } else {
            printf("\n✗ ERROR: %s\n\n", resp.message);
        }
    }
    fflush(stdout);
}

void cmd_see_balance(void) {
    if (!session.authenticated) {
        printf("[ERROR] Must login first!\n");
        fflush(stdout);
        return;
    }

    struct packet_header head = {
        .type = MSG_SEE_BALANCE_REQUEST,
        .length = 0
    };

    send(session.socket_fd, &head, sizeof(head), 0);

    struct packet_header resp_head;
    if (recv_exact(session.socket_fd, &resp_head, sizeof(resp_head)) < 0) {
        printf("[ERROR] Failed to receive response\n");
        fflush(stdout);
        return;
    }

    if (resp_head.type == MSG_SEE_BALANCE_RESPONSE) {
        struct see_balance_response resp;
        memset(&resp, 0, sizeof(resp));
        if (recv_exact(session.socket_fd, &resp, resp_head.length) < 0) {
            printf("[ERROR] Failed to receive response body\n");
            fflush(stdout);
            return;
        }

        if (resp.status == 0) {  // STATUS_SUCCESS
            printf("\n╔════════════ YOUR BALANCE ════════════╗\n");
            printf("║ %s\n", resp.message);
            printf("╚═══════════════════════════════════════╝\n\n");
        } else {
            printf("\n✗ ERROR: %s\n\n", resp.message);
        }
    }
    fflush(stdout);
}

void cmd_status(void) {
    printf("\n========== CLIENT STATUS ==========\n");
    printf("Connected: YES\n");
    printf("Authenticated: %s\n", session.authenticated ? "YES" : "NO");
    if (session.authenticated) printf("Username: %s\n", session.username);
    printf("===================================\n\n");
}

void show_menu(void) {
    printf("\n========== TRADING CLIENT MENU ==========\n");
    printf("  login <user> <pass>  - Login\n");
    printf("  stocks               - View stocks\n");
    printf("  buy <id> <qty> <price> - Buy stock\n");
    printf("  sell <id> <qty> <price> - Sell stock\n");
    printf("  balance              - Check balance\n");
    printf("  status               - Show status\n");
    printf("  help                 - Show menu\n");
    printf("  quit                 - Exit\n");
    printf("========================================\n\n");
}

int main(int argc __attribute__((unused)), char* argv[] __attribute__((unused))) {
    pthread_mutex_init(&session.lock, NULL);
    session.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (session.socket_fd < 0) return 1;
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr.s_addr = inet_addr(SERVER_HOST)
    };
    if (connect(session.socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) return 1;
    printf("[CLIENT] Connected to server\n");
    show_menu();
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_thread_func, NULL);
    printf("[CLIENT] Ready to accept commands. Use 'login' to authenticate.\n\n");
    char input[256];
    while (1) {
        printf(">>> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0';
        if (strlen(input) == 0) continue;
        char cmd[32] = {0};
        char args[224] = {0};
        sscanf(input, "%31s %223[^\n]", cmd, args);
        if (strcmp(cmd, "login") == 0) cmd_login(args);
        else if (strcmp(cmd, "stocks") == 0) cmd_view_stocks();
        else if (strcmp(cmd, "buy") == 0) cmd_buy_stock(args);
        else if (strcmp(cmd, "sell") == 0) cmd_sell_stock(args);
        else if (strcmp(cmd, "balance") == 0) cmd_see_balance();
        else if (strcmp(cmd, "status") == 0) cmd_status();
        else if (strcmp(cmd, "help") == 0) show_menu();
        else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            printf("[CLIENT] Goodbye!\n");
            break;
        } else printf("[ERROR] Unknown command: %s\n", cmd);
        fflush(stdout);
    }
    close(session.socket_fd);
    pthread_mutex_destroy(&session.lock);
    return 0;
}
