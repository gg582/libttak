CC = gcc
CFLAGS = -Wall -Wextra -Werror -O3 -Iinternal -I/usr/local/include
LDFLAGS = -lttak -lcurl -lpthread -lm

BIN_DIR = bin
SRC_DIR = src
TARGET = $(BIN_DIR)/mersenne_searcher

SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/gimps_gateway.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
