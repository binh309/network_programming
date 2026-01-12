#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "view_stocks.h"
#include "../data/stock_db.h"
#include "../network/packet.h"
#include "../network/protocol.h"
#include "../model/error.h"

// Handle view stocks request
void handle_view_stocks_request(int client_socket, const packet_t* request, connection_t* connection) {
    printf("[FEATURE] View stocks request from user %s (ID: %u)\n", connection->username, connection->user_id);

    int stock_count = 0;
    stock_t* stocks = stock_db_get_all(&stock_count);

    // Error handling: Check if retrieval succeeded
    if (!stocks) {
        send_error(client_socket, request->header.request_id, "Database error: Could not retrieve stock list.");
        return;
    }

    if (stock_count == 0) {
        send_error(client_socket, request->header.request_id, "No stocks available in the market.");
        stock_db_free(stocks);
        return;
    }

    // Allocate a large buffer to build the response body
    // NEW FORMAT: "ID,SYMBOL,NAME,BID,BID_QTY,ASK,ASK_QTY,LAST,LAST_QTY,TIMESTAMP;..."
    size_t buffer_size = stock_count * 200; // Increased estimate for new fields
    char* response_body = malloc(buffer_size);
    if (!response_body) {
        send_error(client_socket, request->header.request_id, "Server memory error.");
        stock_db_free(stocks);
        return;
    }

    char* ptr = response_body;
    size_t remaining_size = buffer_size;

    for (int i = 0; i < stock_count; i++) {
        // NEW FORMAT with quantities and timestamp
        int written = snprintf(ptr, remaining_size, 
                               "%u,%s,%s,%.2f,%u,%.2f,%u,%.2f,%u,%ld;",
                               stocks[i].stock_id,
                               stocks[i].symbol,
                               stocks[i].name,
                               stocks[i].best_bid,
                               stocks[i].bid_quantity,        // ← NEW: bid quantity
                               stocks[i].best_ask,
                               stocks[i].ask_quantity,        // ← NEW: ask quantity
                               stocks[i].last_price,
                               stocks[i].last_quantity,       // ← NEW: last quantity
                               stocks[i].last_update_time);   // ← NEW: timestamp
        
        if (written < 0 || (size_t)written >= remaining_size) {
            // Error: Buffer would overflow - too many stocks to fit
            fprintf(stderr, "[FEATURE] Stock data too large for response packet\n");
            send_error(client_socket, request->header.request_id, 
                      "Too many stocks to fit in response. Consider pagination.");
            free(response_body);
            stock_db_free(stocks);
            return;
        }
        ptr += written;
        remaining_size -= written;
    }

    // Verify final response size fits in packet
    size_t response_size = ptr - response_body;
    if (response_size > MAX_BODY_LEN) {
        fprintf(stderr, "[FEATURE] Response body exceeds maximum packet size\n");
        send_error(client_socket, request->header.request_id, 
                  "Response too large to send.");
        free(response_body);
        stock_db_free(stocks);
        return;
    }

    packet_t response;
    create_packet(&response, request->header.request_id, SMSG_VIEW_STOCKS_DATA, response_body);
    send_packet(client_socket, &response);

    free(response_body);
    stock_db_free(stocks);

    printf("[FEATURE] Sent %d stocks to user %s (data format: ID,SYMBOL,NAME,BID,BID_QTY,ASK,ASK_QTY,LAST,LAST_QTY,TIMESTAMP)\n", 
           stock_count, connection->username);
}