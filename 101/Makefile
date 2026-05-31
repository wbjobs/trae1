CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -std=c99
LDFLAGS = -pthread -lm

TARGET = db_dispatcher
SRC = db_dispatcher.c
OBJ = $(SRC:.c=.o)

PREFIX ?= /usr/local
SBINDIR = $(PREFIX)/sbin
CONFDIR = $(PREFIX)/etc/db_dispatcher
INITDIR = $(PREFIX)/etc/init.d

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c db_dispatcher.h
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -d $(SBINDIR)
	install -d $(CONFDIR)
	install -m 755 $(TARGET) $(SBINDIR)/
	install -m 644 db_dispatcher.conf.example $(CONFDIR)/

uninstall:
	rm -f $(SBINDIR)/$(TARGET)
	rm -rf $(CONFDIR)

clean:
	rm -f $(TARGET) $(OBJ)

distclean: clean
	rm -f keepalived.conf lvs_setup.sh

.PHONY: all install uninstall clean distclean
