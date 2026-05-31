CC := gcc
CFLAGS := -Wall -Wextra -Werror -g -O2 -std=c11 -D_GNU_SOURCE
CFLAGS += -Iinclude
LDFLAGS := -lsctp -lpthread -lm

SRCDIR := src
BUILDDIR := build
TARGET := sctp_transfer

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
