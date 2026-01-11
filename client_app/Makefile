CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.
LDFLAGS = -lm -lpthread

# Source files
CLIENT_SOURCES = client.c packet.c
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)
CLIENT_BIN = client

.PHONY: all clean run

all: $(CLIENT_BIN)

$(CLIENT_BIN): $(CLIENT_OBJECTS)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_OBJECTS) $(LDFLAGS)
	@echo "[BUILD] Client compiled: $(CLIENT_BIN)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CLIENT_OBJECTS) $(CLIENT_BIN)
	@echo "[CLEAN] Client build artifacts removed"

run: $(CLIENT_BIN)
	@echo "[RUN] Starting client..."
	./$(CLIENT_BIN)
