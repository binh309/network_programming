#ifndef SELL_STOCK_H
#define SELL_STOCK_H

#include <stdint.h>
#include "../network/protocol.h"

// Function declarations
void handle_sell_stock_request(int client_fd, struct sell_stock_request* request);

#endif
