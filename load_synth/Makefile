# Load Synthesizer Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -pthread

TARGET = synth
SRCS = synth.c worker.c stats.c packet.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)
	@echo "[BUILD] Load synthesizer compiled: $(TARGET)"

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c synth.h protocol.h packet.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "[CLEAN] Build artifacts removed"
