CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.
LDFLAGS = -lm -lpthread

# Source files
SERVER_SOURCES = core/server.c core/event_loop.c core/connection_manager.c core/connection_hash.c core/request_handler.c core/portfolio_manager.c data/account_db.c data/stock_db.c data/portfolio_db.c data/transaction_db.c features/view_stocks.c features/dispatcher.c features/market.c features/buy_stock.c features/sell_stock.c features/see_balance.c features/register.c features/login.c features/see_my_stocks.c network/packet.c model/error.c network/socket_io.c network/network_send.c network/network_receive.c network/packet_parser.c network/packet_builder.c model/portfolio.c

# Object files
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)

# Targets
SERVER_BIN = server

.PHONY: all clean server run

all: server

server: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_OBJECTS) $(LDFLAGS)
	@echo "[BUILD] Server compiled: $(SERVER_BIN)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJECTS) $(SERVER_BIN)
	@echo "[CLEAN] Build artifacts removed"

run: server
	@echo "[RUN] Starting server..."
	./$(SERVER_BIN)