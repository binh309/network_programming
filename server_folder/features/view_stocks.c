#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "view_stocks.h"
#include "../data/stock_db.h"

// Handle view stocks request - return list of all available stocks
void handle_view_stocks_request(int client_fd) {
    printf("[FEATURE] View stocks request from fd=%d\n", client_fd);

    // Get all stocks from database
    int stock_count = 0;
    stock_t* stocks = stock_db_get_all(&stock_count);

    if (!stocks || stock_count == 0) {
        printf("[FEATURE] No stocks found\n");
        
        struct packet_header resp_hdr = {
            .type = MSG_VIEW_STOCKS_RESPONSE,
            .length = sizeof(struct view_stocks_response)
        };

        struct view_stocks_response resp = {
            .status = STATUS_FAILED,
            .stock_count = 0
        };

        send(client_fd, &resp_hdr, sizeof(resp_hdr), 0);
        send(client_fd, &resp, sizeof(resp), 0);
        return;
    }

    // Build response header
    struct packet_header resp_hdr = {
        .type = MSG_VIEW_STOCKS_RESPONSE,
        .length = sizeof(struct view_stocks_response) + (stock_count * sizeof(struct stock_info))
    };

    // Send header
    send(client_fd, &resp_hdr, sizeof(resp_hdr), 0);

    // Send response with stock count
    struct view_stocks_response resp = {
        .status = STATUS_SUCCESS,
        .stock_count = stock_count
    };
    send(client_fd, &resp, sizeof(resp), 0);

    // Send each stock
    for (int i = 0; i < stock_count; i++) {
        struct stock_info info;
        info.stock_id = stocks[i].stock_id;
        strncpy(info.symbol, stocks[i].symbol, 15);
        info.symbol[15] = '\0';
        info.current_price = stocks[i].current_price;
        info.available_quantity = stocks[i].available_quantity;

        send(client_fd, &info, sizeof(info), 0);
        printf("[FEATURE] Sent stock: %s @ $%.2f (%u available)\n", info.symbol, info.current_price, info.available_quantity);
    }

    printf("[FEATURE] View stocks response sent to fd=%d (%d stocks)\n", client_fd, stock_count);
}
