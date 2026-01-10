#include <stdio.h>
#include <string.h>
#include "dispatcher.h"
#include "view_stocks.h"
#include "buy_stock.h"
#include "sell_stock.h"
#include "see_balance.h"
#include "../core/server.h"

// Route incoming messages to appropriate handlers
void dispatcher_handle_message(int client_fd, struct packet_header* header, char* payload) {
    printf("[DISPATCHER] Handling message type=0x%02x from fd=%d\n", header->type, client_fd);

    switch (header->type) {
        case MSG_LOGIN_REQUEST:
            if (header->length >= sizeof(struct login_payload)) {
                struct login_payload* login = (struct login_payload*)payload;
                handle_login_request(client_fd, login);
            }
            break;

        case MSG_VIEW_STOCKS_REQUEST:
            handle_view_stocks_request(client_fd);
            break;

        case MSG_BUY_STOCK_REQUEST:
            if (header->length >= sizeof(struct buy_stock_request)) {
                struct buy_stock_request* buy = (struct buy_stock_request*)payload;
                handle_buy_stock_request(client_fd, buy);
            }
            break;

        case MSG_SELL_STOCK_REQUEST:
            if (header->length >= sizeof(struct sell_stock_request)) {
                struct sell_stock_request* sell = (struct sell_stock_request*)payload;
                handle_sell_stock_request(client_fd, sell);
            }
            break;

        case MSG_SEE_BALANCE_REQUEST:
            handle_see_balance_request(client_fd);
            break;

        default:
            printf("[DISPATCHER] Unknown message type: 0x%02x\n", header->type);
            break;
    }
}
