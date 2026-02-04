CC ?= tcc
AR ?= ar

COMMON_WARNINGS = -Wall -std=c17 -pthread -Iinclude -D_GNU_SOURCE -D_XOPEN_SOURCE=700
DEPFLAGS = -MD -MF $(@:.o=.d)
LDFLAGS_BASE = -pthread -lm

# Detect which compiler family we are using.
BUILD_PROFILE = perf
ifneq (,$(findstring tcc,$(notdir $(CC))))
BUILD_PROFILE = tcc
endif

TCC_STACK_FLAGS = -O3 -g \
                  -fno-inline \
                  -fno-omit-frame-pointer \
                  -fno-optimize-sibling-calls

PERF_WARNINGS = -Wextra -Wshadow -Wstrict-prototypes -Wswitch-enum
PERF_STACK_FLAGS = -O3 -march=native -mtune=native -pipe -flto -ffat-lto-objects \
                   -fomit-frame-pointer -funroll-loops \
                   -fstrict-aliasing -ffunction-sections -fdata-sections \
                   -fvisibility=hidden -DNDEBUG

ifeq ($(BUILD_PROFILE),tcc)
CFLAGS = $(COMMON_WARNINGS) $(TCC_STACK_FLAGS)
LDFLAGS = $(LDFLAGS_BASE)
else
CFLAGS = $(COMMON_WARNINGS) $(PERF_WARNINGS) $(PERF_STACK_FLAGS)
LDFLAGS = $(LDFLAGS_BASE) -flto -Wl,--gc-sections
endif

CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

SRC_DIRS = src/ht src/thread src/timing src/mem src/async src/priority \
           src/atomic src/sync src/math src/tree src/container \
           src/security src/mem_tree src/limit src/stats src/log \
           src/unsafe

SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst src/%.c,obj/%.o,$(SRCS))
ASMS = $(patsubst src/%.c,obj/%.s,$(SRCS))
DEPS = $(OBJS:.o=.d)

LIB = lib/libttak.a

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(TEST_SRCS:.c=)

all: directories $(LIB)

$(LIB): $(OBJS)
	@mkdir -p lib
	$(AR) rcs $@ $(OBJS)

asm: directories $(ASMS)

# Default rule for all other directories
obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

obj/%.s: obj/%.o
	@mkdir -p $(dir $@)
	objdump -d $< > $@

test: all $(TEST_BINS)
	@echo "Starting internal test suite..."
	@for bin in $(TEST_BINS); do \
		echo "[RUNNING] $$bin"; \
		./$$bin; \
		if [ $$? -eq 0 ]; then \
			echo "[PASSED] $$bin"; \
		else \
			echo "[FAILED] $$bin with exit code $$?"; \
			exit 1; \
		fi \
	done
	@echo "All tests completed successfully."

tests/test_%: tests/test_%.c $(LIB)
	$(CC) $(CFLAGS) $< -o $@ $(LIB) $(LDFLAGS)

directories:
	@mkdir -p $(foreach dir,$(SRC_DIRS),obj/$(patsubst src/%,%,$(dir))) lib

clean:
	rm -rf obj lib tests/*.d
	find tests/ -type f ! -name "*.c" ! -name "*.h" -delete
	rm -rf a.out

asm_clean:
	rm -f $(ASMS)

install: $(LIB)
	install -d $(INCDIR)
	install -d $(LIBDIR)
	cp -r include/* $(INCDIR)/
	install -m 644 $(LIB) $(LIBDIR)/

uninstall:
	rm -rf $(INCDIR)/ttak
	rm -f $(LIBDIR)/libttak.a

blueprints:
	@python3 blueprints/scripts/render_blueprints.py

-include $(DEPS)

.PHONY: all clean directories install uninstall blueprints test asm asm_clean
