CC      ?= gcc
CFLAGS  ?= -O2 -g
override CFLAGS  += -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Iinclude -pthread -MMD -MP
override LDFLAGS += -pthread

BUILD   := build
SRCS    := $(wildcard src/*.c)
LIB_SRCS := $(filter-out src/main.c,$(SRCS))
LIB_OBJS := $(LIB_SRCS:src/%.c=$(BUILD)/%.o)
MAIN_OBJ := $(BUILD)/main.o
TEST_SRCS := $(wildcard tests/*.c)
TEST_BINS := $(TEST_SRCS:tests/%.c=$(BUILD)/%)

.PHONY: all test clean debug tsan

all: sim

sim: $(MAIN_OBJ) $(LIB_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%: tests/%.c $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; ./$$t || exit 1; done

# Rebuild from scratch with thread sanitizer
tsan: clean
	$(MAKE) CFLAGS="-O1 -g -fsanitize=thread" LDFLAGS="-fsanitize=thread" all test

clean:
	rm -rf $(BUILD) sim

-include $(wildcard $(BUILD)/*.d)
