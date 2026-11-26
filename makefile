# ====================# wa-tor ecosystem simulation #====================
#
# Builds:
#   ecosystem          the program (src/main.c + libwator.a + libquadtree.a)
#   test/wator/twator  the library tests
#
# Targets:
#   make             build everything
#   make test        build + run the tests
#   make tsan        rebuild with -fsanitize=thread and run the tests
#   make clean       remove build dirs and binaries

# Compiler and flags
CC      ?= gcc
CFLAGS  ?= -O2 -g
override CFLAGS  += -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Ilib/wator/include -Ilib/quadtree/include -pthread
override LDFLAGS += -pthread

# Directory structure
LIB      := lib/wator
LIB_SRCS := $(wildcard $(LIB)/src/*.c)
LIB_OBJS := $(LIB_SRCS:$(LIB)/src/%.c=$(LIB)/build/%.o)
LIB_BIN  := $(LIB)/build/libwator.a
QT       := lib/quadtree
QT_SRCS  := $(wildcard $(QT)/src/*.c)
QT_OBJS  := $(QT_SRCS:$(QT)/src/%.c=$(QT)/build/%.o)
QT_BIN   := $(QT)/build/libquadtree.a
TEST_SRC := test/wator/twator.c
TEST_BIN := $(TEST_SRC:.c=)

.PHONY: all test tsan clean

all: ecosystem $(TEST_BIN)

ecosystem: src/main.c $(LIB_BIN) $(QT_BIN)
	$(CC) $(CFLAGS) $< $(LIB_BIN) $(QT_BIN) -o $@ $(LDFLAGS)

$(LIB_BIN): $(LIB_OBJS)
	ar rcs $@ $^

$(LIB)/build/%.o: $(LIB)/src/%.c | $(LIB)/build
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB)/build:
	mkdir -p $(LIB)/build

$(QT_BIN): $(QT_OBJS)
	ar rcs $@ $^

$(QT)/build/%.o: $(QT)/src/%.c | $(QT)/build
	$(CC) $(CFLAGS) -c $< -o $@

$(QT)/build:
	mkdir -p $(QT)/build

$(TEST_BIN): $(TEST_SRC) $(LIB_BIN) $(QT_BIN)
	$(CC) $(CFLAGS) $< $(LIB_BIN) $(QT_BIN) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

# Rebuild from scratch with the thread sanitizer
tsan: clean
	$(MAKE) CFLAGS="-O1 -g -fsanitize=thread -Wno-attributes" LDFLAGS="-fsanitize=thread" all test

clean:
	rm -rf $(LIB)/build $(QT)/build test/wator/twator ecosystem
