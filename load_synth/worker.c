/**
 * worker.c - Worker thread implementation
 * 
 * Each worker represents one simulated client connection
 */

#include "synth.h"
#include "protocol.h"
#include "packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

// Global stock list
stock_info_t g_stocks[MAX_STOCKS];
int g_num_stocks = 0;

// Connect to server
static int worker_connect(worker_t* w) {
    w->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (w->sockfd < 0) {
        fprintf(stderr, "[W%d] socket() failed: %s\n", w->worker_id, strerror(errno));
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(w->config->port);
    
    if (inet_pton(AF_INET, w->config->host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[W%d] inet_pton failed for %s: %s\n", w->worker_id, w->config->host, strerror(errno));
        close(w->sockfd);
        return -1;
    }
    
    fprintf(stderr, "[W%d] Connecting to %s:%d...\n", w->worker_id, w->config->host, w->config->port);
    
    if (connect(w->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[W%d] connect() failed: %s\n", w->worker_id, strerror(errno));
        close(w->sockfd);
        return -1;
    }
    
    fprintf(stderr, "[W%d] Connected!\n", w->worker_id);
    w->connected = true;
    return 0;
}

// Login to server
static int worker_login(worker_t* w) {
    packet_t req, resp;
    char body[128];
    
    // Format: USERNAME,PASSWORD (comma-separated)
    snprintf(body, sizeof(body), "%s,%s", w->username, w->password);
    create_packet(&req, 1, CMSG_LOGIN, body);
    
    fprintf(stderr, "[W%d] Sending login for %s...\n", w->worker_id, w->username);
    
    if (send_packet(w->sockfd, &req) < 0) {
        fprintf(stderr, "[W%d] send_packet failed for login\n", w->worker_id);
        return -1;
    }
    
    fprintf(stderr, "[W%d] Waiting for login response...\n", w->worker_id);
    
    if (receive_packet(w->sockfd, &resp) < 0) {
        fprintf(stderr, "[W%d] receive_packet failed for login\n", w->worker_id);
        return -1;
    }
    
    fprintf(stderr, "[W%d] Login response type: 0x%02x, body: %s\n", w->worker_id, resp.header.type, resp.body);
    
    if (resp.header.type == SMSG_LOGIN_SUCCESS) {
        // Parse user_id from response
        sscanf(resp.body, "%u", &w->user_id);
        w->logged_in = true;
        fprintf(stderr, "[W%d] Login SUCCESS, user_id=%u\n", w->worker_id, w->user_id);
        return 0;
    }
    
    fprintf(stderr, "[W%d] Login FAILED\n", w->worker_id);
    return -1;
}

// View stocks (to get current prices)
static int worker_view_stocks(worker_t* w) {
    packet_t req, resp;
    
    create_packet(&req, 2, CMSG_VIEW_STOCKS, "");
    
    fprintf(stderr, "[W%d] Fetching stock list...\n", w->worker_id);
    
    if (send_packet(w->sockfd, &req) < 0) {
        fprintf(stderr, "[W%d] Failed to send VIEW_STOCKS\n", w->worker_id);
        return -1;
    }
    
    if (receive_packet(w->sockfd, &resp) < 0) {
        fprintf(stderr, "[W%d] Failed to receive VIEW_STOCKS response\n", w->worker_id);
        return -1;
    }
    
    fprintf(stderr, "[W%d] VIEW_STOCKS response type: 0x%02x\n", w->worker_id, resp.header.type);
    fprintf(stderr, "[W%d] VIEW_STOCKS body: %.200s\n", w->worker_id, resp.body);
    
    if (resp.header.type != SMSG_VIEW_STOCKS_DATA) {
        fprintf(stderr, "[W%d] VIEW_STOCKS failed: type=0x%02x\n", w->worker_id, resp.header.type);
        return -1;
    }
    
    // Parse stock data: "ID,SYMBOL,NAME,BID,BID_VOL,ASK,ASK_VOL,LAST,LAST_VOL,TIMESTAMP;..."
    g_num_stocks = 0;
    char* line = strtok(resp.body, ";");
    while (line && g_num_stocks < MAX_STOCKS) {
        stock_info_t* s = &g_stocks[g_num_stocks];
        char name[64];
        int bid_vol, ask_vol, last_vol;
        double last;
        long timestamp;
        // Format: ID,SYMBOL,NAME,BID,BID_VOL,ASK,ASK_VOL,LAST,LAST_VOL,TIMESTAMP
        if (sscanf(line, "%hu,%15[^,],%63[^,],%lf,%d,%lf,%d,%lf,%d,%ld", 
                   &s->stock_id, s->symbol, name, &s->bid, &bid_vol, 
                   &s->ask, &ask_vol, &last, &last_vol, &timestamp) >= 6) {
            fprintf(stderr, "[W%d] Parsed stock: %s (ID %d) bid=%.2f ask=%.2f\n", 
                    w->worker_id, s->symbol, s->stock_id, s->bid, s->ask);
            g_num_stocks++;
        } else {
            fprintf(stderr, "[W%d] Failed to parse: %s\n", w->worker_id, line);
        }
        line = strtok(NULL, ";");
    }
    
    fprintf(stderr, "[W%d] Loaded %d stocks\n", w->worker_id, g_num_stocks);
    return 0;
}

// Execute a buy order
static int worker_buy(worker_t* w, uint16_t stock_id, uint32_t quantity, double ask_price, uint64_t* latency_us) {
    packet_t req, resp;
    char body[64];
    struct timespec start, end;
    
    // Format: stock_id,quantity,price,type
    // Use ask price for buying (MARKET type will use market price anyway)
    snprintf(body, sizeof(body), "%u,%u,%.2f,MARKET", stock_id, quantity, ask_price);
    create_packet(&req, (w->worker_id * 1000) + (rand_r(&w->rand_state) % 1000), CMSG_BUY_STOCK, body);
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    if (send_packet(w->sockfd, &req) < 0) {
        fprintf(stderr, "[W%d] BUY send failed\n", w->worker_id);
        return -2;  // Connection error
    }
    
    if (receive_packet(w->sockfd, &resp) < 0) {
        fprintf(stderr, "[W%d] BUY recv failed\n", w->worker_id);
        return -2;  // Connection error
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    *latency_us = (end.tv_sec - start.tv_sec) * 1000000 + 
                  (end.tv_nsec - start.tv_nsec) / 1000;
    
    if (resp.header.type != SMSG_BUY_STOCK_SUCCESS) {
        // Only print first few failures to avoid spam
        static int fail_count = 0;
        if (fail_count++ < 5) {
            fprintf(stderr, "[W%d] BUY FAIL: type=0x%02x body=%s\n", w->worker_id, resp.header.type, resp.body);
        }
    }
    
    return (resp.header.type == SMSG_BUY_STOCK_SUCCESS) ? 0 : -1;
}

// Execute a sell order
static int worker_sell(worker_t* w, uint16_t stock_id, uint32_t quantity, double bid_price, uint64_t* latency_us) {
    packet_t req, resp;
    char body[64];
    struct timespec start, end;
    
    // Format: stock_id,quantity,price,type
    // Use bid price for selling (MARKET type will use market price anyway)
    snprintf(body, sizeof(body), "%u,%u,%.2f,MARKET", stock_id, quantity, bid_price);
    create_packet(&req, (w->worker_id * 1000) + (rand_r(&w->rand_state) % 1000), CMSG_SELL_STOCK, body);
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    if (send_packet(w->sockfd, &req) < 0) {
        return -2;  // Connection error
    }
    
    if (receive_packet(w->sockfd, &resp) < 0) {
        return -2;  // Connection error
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    *latency_us = (end.tv_sec - start.tv_sec) * 1000000 + 
                  (end.tv_nsec - start.tv_nsec) / 1000;
    
    return (resp.header.type == SMSG_SELL_STOCK_SUCCESS) ? 0 : -1;
}

// Disconnect
static void worker_disconnect(worker_t* w) {
    if (w->sockfd >= 0) {
        // Send logout
        packet_t req;
        create_packet(&req, 0, CMSG_LOGOUT, "");
        send_packet(w->sockfd, &req);
        
        close(w->sockfd);
        w->sockfd = -1;
    }
    w->connected = false;
    w->logged_in = false;
}

// Worker thread main function
void* worker_thread(void* arg) {
    worker_t* w = (worker_t*)arg;
    
    // Setup test account credentials
    snprintf(w->username, sizeof(w->username), "test%d", w->worker_id + 1);
    snprintf(w->password, sizeof(w->password), "test%d", w->worker_id + 1);
    w->rand_state = w->worker_id + time(NULL);
    
    // Connect
    if (worker_connect(w) < 0) {
        stats_record_connect(w->stats, false);
        if (w->config->verbose) {
            fprintf(stderr, "[W%d] Connection failed\n", w->worker_id);
        }
        return NULL;
    }
    stats_record_connect(w->stats, true);
    
    // Login
    if (worker_login(w) < 0) {
        if (w->config->verbose) {
            fprintf(stderr, "[W%d] Login failed for %s\n", w->worker_id, w->username);
        }
        worker_disconnect(w);
        return NULL;
    }
    
    if (w->config->verbose) {
        fprintf(stderr, "[W%d] Logged in as %s (ID: %u)\n", w->worker_id, w->username, w->user_id);
    }
    
    // Get stock list (only first worker does this)
    if (w->worker_id == 0) {
        worker_view_stocks(w);
    }
    
    // Wait for stock list to be populated
    while (g_num_stocks == 0 && w->stats->running) {
        usleep(10000);  // 10ms
    }
    
    // Main trading loop
    while (w->stats->running) {
        // Check for pause
        while (w->stats->paused && w->stats->running) {
            usleep(100000);  // 100ms
        }
        
        if (!w->stats->running) break;
        
        // Select random stock and quantity
        int stock_idx = rand_r(&w->rand_state) % g_num_stocks;
        stock_info_t* stock = &g_stocks[stock_idx];
        uint32_t quantity = 1 + (rand_r(&w->rand_state) % 10);  // 1-10 shares (smaller for test accounts)
        
        uint64_t latency_us = 0;
        int result;
        
        // 70% buy, 30% sell
        if ((rand_r(&w->rand_state) % 100) < 70) {
            result = worker_buy(w, stock->stock_id, quantity, stock->ask, &latency_us);
        } else {
            result = worker_sell(w, stock->stock_id, quantity, stock->bid, &latency_us);
        }
        
        if (result == -2) {
            // Connection error
            w->stats->connection_errors++;
            break;
        }
        
        stats_record_order(w->stats, result == 0, latency_us);
        
        // Rate limiting
        if (w->config->target_rate > 0) {
            // Sleep to achieve target rate across all workers
            int sleep_us = 1000000 / w->config->target_rate * w->config->num_clients;
            usleep(sleep_us);
        } else {
            // Small sleep to not overwhelm
            usleep(1000);  // 1ms between orders
        }
    }
    
    // Disconnect
    worker_disconnect(w);
    stats_record_disconnect(w->stats);
    
    return NULL;
}
