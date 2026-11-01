CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -fPIC -Iinclude
LDFLAGS ?= -lpthread

SRCS = src/bridge_p2p.c
OBJS = $(SRCS:.c=.o)
TARGET_LIB = libbridge_p2p.so
TARGET_TEST = test_p2p_core

all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET_LIB) test/test_p2p_core.c
	$(CC) $(CFLAGS) -o $(TARGET_TEST) test/test_p2p_core.c -L. -lbridge_p2p $(LDFLAGS)
	LD_LIBRARY_PATH=. ./$(TARGET_TEST)

clean:
	rm -f src/*.o $(TARGET_LIB) $(TARGET_TEST)

.PHONY: all test clean
