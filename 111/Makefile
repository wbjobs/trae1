CC = gcc
CFLAGS = -Wall -O2 -fno-strict-aliasing
LDFLAGS = -lnetfilter_queue -lnfnetlink -lpcre -lpthread
TARGET = mysql_firewall
SRC = mysql_firewall.c tcp_reassembly.c mysql_parser.c injection_detect.c whitelist.c stmt_cache.c sandbox.c learning.c
HDR = mysql_firewall.h tcp_reassembly.h mysql_parser.h injection_detect.h whitelist.h config.h stmt_cache.h sandbox.h learning.h

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

test: test_binary_protocol.c
	$(CC) $(CFLAGS) -o test_binary test_binary_protocol.c mysql_parser.c stmt_cache.c $(LDFLAGS)

clean:
	rm -f $(TARGET) test_binary

install: $(TARGET)
	@echo "Installation requires root privileges"
	@echo "Run: cp $(TARGET) /usr/local/bin/"
	@echo "And setup iptables: iptables -I OUTPUT -p tcp --dport 3306 -j NFQUEUE --queue-num 0"
	@echo "           iptables -I INPUT -p tcp --sport 3306 -j NFQUEUE --queue-num 0"

