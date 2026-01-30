CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -I/usr/local/include
LDFLAGS = -lttak -lpthread -lm

TARGET = mersenne_explorer
SRCS = mersenne_explorer.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
