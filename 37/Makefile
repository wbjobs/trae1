CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c11
LDFLAGS ?=
LDLIBS  ?= -lpcap -lpthread

TARGET  = mysql-sniffer
SRCS    = main.c capture.c tcp_reasm.c mysql_parser.c filter.c output.c alert.c utils.c
OBJS    = $(SRCS:.c=.o)
HDRS    = config.h capture.h tcp_reasm.h mysql_parser.h filter.h output.h alert.h utils.h

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man/man1

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
