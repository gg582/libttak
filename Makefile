CC = gcc
CFLAGS = -Wall -pthread -MMD -MP -Iinclude
LDFLAGS = -pthread

PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

SRC_DIRS = src/ht src/thread src/timing src/mem src/async src/priority src/atomic src/sync src/math src/tree src/container src/security src/mem_tree
SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst src/%.c,obj/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

LIB = lib/libttak.a

TEST_SRCS = $(wildcard tests/test_*.c) tests/test_security.c
TEST_BINS = $(patsubst tests/test_%.c,tests/test_%,$(TEST_SRCS))

all: directories $(LIB)

$(LIB): $(OBJS)
	@mkdir -p lib
	ar rcs $@ $(OBJS)
	@echo "Library built: $@"

obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Installation Paths ---
PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

# --- Installation Targets ---
.PHONY: install
install: $(LIB)
	@echo "Installing libttak to $(PREFIX)..."
	@# Create necessary directories
	install -d $(INCDIR)/ttak
	install -d $(LIBDIR)

	@# Copy headers maintaining the directory structure
	@# This allows #include <ttak/...> style usage
	cp -r include/* $(INCDIR)/

	@# Install the static library with secure 644 permissions
	install -m 644 $(LIB) $(LIBDIR)/

	@echo "Installation Complete!"

.PHONY: uninstall
uninstall:
	@echo "Removing libttak from $(PREFIX)..."
	@# Remove headers and the static library
	rm -rf $(INCDIR)/ttak
	rm -f $(LIBDIR)/libttak.a
	@echo "Uninstallation complete."

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
