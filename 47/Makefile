CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -Wpedantic -std=gnu11 -D_GNU_SOURCE
LDFLAGS  ?=
LDLIBS   ?= -lcgroup -lpthread

SRC_DIR  := src
BUILD_DIR:= build
BIN      := mycontainer

SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean install

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

install: $(BIN)
	install -m 4755 $(BIN) /usr/local/bin/$(BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN)
