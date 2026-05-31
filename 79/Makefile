CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11 -mconsole
INCLUDES = -Iinclude
LDFLAGS  = -lusb-1.0 -lpthread

TARGET   = usb_flash
SRCDIR   = src
OBJDIR   = obj

USE_LIBSODIUM ?= 0

ifeq ($(USE_LIBSODIUM),1)
    CFLAGS += -DUSE_LIBSODIUM
    LDFLAGS += -lsodium
endif

SOURCES  = $(wildcard $(SRCDIR)/*.c)
OBJECTS  = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

sodium:
	$(MAKE) USE_LIBSODIUM=1

clean:
	rm -rf $(OBJDIR) $(TARGET)