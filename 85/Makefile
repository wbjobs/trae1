CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c11 -O2 -D_GNU_SOURCE
LDFLAGS = -lm

TARGET  = hrng_eval
SRCS    = main.c spi.c rng_reader.c nist_tests.c report.c csv_export.c
OBJS    = $(SRCS:.c=.o)
HDRS    = config.h spi.h rng_reader.h nist_tests.h report.h csv_export.h

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)
