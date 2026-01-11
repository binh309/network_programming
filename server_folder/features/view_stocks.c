#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "view_stocks.h"
#include "../data/stock_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

// Handle view stocks request
void handle_view_stocks_request(int client_socket, const packet_t* request, session_t* session) {
    printf("[FEATURE] View stocks request from user %s (ID: %u)\n", session->username, session->user_id);

    int stock_count = 0;
    stock_t* stocks = stock_db_get_all(&stock_count);

    if (!stocks || stock_count == 0) {
        send_error(client_socket, request->header.request_id, "No stocks available in the market.");
        if (stocks) stock_db_free(stocks);
        return;
    }

    // Allocate a large buffer to build the response body
    // Format: "ID,SYMBOL,NAME,BID,ASK,LAST_PRICE;..."
    size_t buffer_size = stock_count * 128; // Estimate size
    char* response_body = malloc(buffer_size);
    if (!response_body) {
        send_error(client_socket, request->header.request_id, "Server memory error.");
        stock_db_free(stocks);
        return;
    }

    char* ptr = response_body;
    size_t remaining_size = buffer_size;

    for (int i = 0; i < stock_count; i++) {
        int written = snprintf(ptr, remaining_size, "%u,%s,%s,%.2f,%.2f,%.2f;",
                               stocks[i].stock_id,
                               stocks[i].symbol,
                               stocks[i].name,
                               stocks[i].best_bid,
                               stocks[i].best_ask,
                               stocks[i].last_price);
        
        if (written < 0 || (size_t)written >= remaining_size) {
            // Error or buffer too small
            break;
        }
        ptr += written;
        remaining_size -= written;
    }

    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_VIEW_STOCKS_DATA, response_body);
    send_packet(client_socket, &response);

    free(response_body);
    stock_db_free(stocks);

    printf("[FEATURE] Sent %d stocks to user %s\n", stock_count, session->username);
}