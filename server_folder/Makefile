CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.
LDFLAGS = -lm -lpthread

# Source files
SERVER_SOURCES = core/server.c core/session_manager.c data/account_db.c data/stock_db.c data/portfolio_db.c features/view_stocks.c features/dispatcher.c features/market.c features/buy_stock.c features/sell_stock.c features/see_balance.c features/register.c features/login.c features/see_my_stocks.c network/packet.c model/error.c
CLIENT_SOURCES = ../client_app/client.c network/packet.c

# Object files
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

# Targets
SERVER_BIN = server
CLIENT_BIN = ../client_app/client

.PHONY: all clean server client run

all: server client

server: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_OBJECTS) $(LDFLAGS)
	@echo "[BUILD] Server compiled: $(SERVER_BIN)"

client: $(CLIENT_OBJECTS)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_OBJECTS) $(LDFLAGS)
	@echo "[BUILD] Client compiled: $(CLIENT_BIN)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJECTS) $(CLIENT_OBJECTS) $(SERVER_BIN) $(CLIENT_BIN)
	@echo "[CLEAN] Build artifacts removed"

run: server
	@echo "[RUN] Starting server..."
	./$(SERVER_BIN)