CC = gcc
# Build the aliqout tracker against the in-tree libttak to avoid system-wide installs.
ROOT = ../..
LIBTTK = $(ROOT)/lib/libttak.a
CFLAGS = -Wall -Wextra -Werror -O3 -pthread -I$(ROOT)/include -march=native -g
LDFLAGS = $(LIBTTK) -pthread -lm

BIN_DIR = bin
SRC_DIR = src
TARGET = $(BIN_DIR)/aliquot_tracker

SRCS = $(SRC_DIR)/main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) $(LIBTTK)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBTTK):
	$(MAKE) -C $(ROOT) lib/libttak.a

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
