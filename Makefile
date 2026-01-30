CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -pthread -MMD -MP -Iinclude
LDFLAGS = -pthread

PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

SRC_DIRS = src/ht src/thread src/timing src/mem src/async src/priority src/atomic src/sync src/math
SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst src/%.c,obj/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

LIB = lib/libttak.a

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(patsubst tests/test_%.c,tests/test_%,$(TEST_SRCS))

all: directories $(LIB)

$(LIB): $(OBJS)
	@mkdir -p lib
	ar rcs $@ $(OBJS)
	@echo "Library built: $@"

obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: all $(TEST_BINS)
	@for bin in $(TEST_BINS); do \
		echo "Running $$bin with timeout..."; \
		timeout 5s ./$$bin || exit 1; \
	done
	@echo "All tests passed!"

tests/test_%: tests/test_%.c $(LIB)
	$(CC) $(CFLAGS) $< -o $@ $(LIB) $(LDFLAGS)

directories:
	@mkdir -p $(foreach dir,$(SRC_DIRS),obj/$(patsubst src/%,%,$(dir))) lib

clean:
	rm -rf obj lib $(TEST_BINS) tests/*.d

-include $(DEPS)

.PHONY: all clean directories install uninstall