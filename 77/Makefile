CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE
LDFLAGS  ?= -lpthread

TARGET   := vshaper
SRCS     := tun_dev.c tc_shaper.c cli_parser.c config_file.c stats.c \
            dry_run.c cron_scheduler.c http_api.c rule_history.c \
            pcap_replay.c main.c
OBJS     := $(SRCS:.c=.o)
HDRS     := common.h tun_dev.h tc_shaper.h cli_parser.h config_file.h \
            stats.h dry_run.h cron_scheduler.h http_api.h rule_history.h \
            pcap_replay.h

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/sbin

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
