#ifndef BUY_STOCK_H
#define BUY_STOCK_H

#include <stdint.h>
#include "../network/protocol.h"

// Function declarations
void handle_buy_stock_request(int client_fd, struct buy_stock_request* request);

#endif
