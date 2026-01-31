CC = gcc
CFLAGS = -Wall -Wextra -Werror -O3 -Iinternal -Iinclude -I/usr/local/include -march=native
LDFLAGS = -lttak -lpthread -lm

BIN_DIR = bin
SRC_DIR = src
TARGET = $(BIN_DIR)/aliquot_tracker

SRCS = $(SRC_DIR)/main.c
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
