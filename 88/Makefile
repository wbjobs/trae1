# SPDX-License-Identifier: BSD-3-Clause
# DPDK Traffic Parsing Engine Makefile

PKGCONF = pkg-config

CFLAGS += -O3 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
CFLAGS += -std=gnu11
CFLAGS += -D_GNU_SOURCE
CFLAGS += -DALLOW_EXPERIMENTAL_API

# DPDK
CFLAGS  += $(shell $(PKGCONF) --cflags libdpdk)
LDFLAGS += $(shell $(PKGCONF) --libs libdpdk)

# ONNX Runtime C API (install from https://github.com/microsoft/onnxruntime)
# Set ONNXRUNTIME_ROOT=/path/to/onnxruntime when building
ifneq ($(ONNXRUNTIME_ROOT),)
CFLAGS  += -I$(ONNXRUNTIME_ROOT)/include
LDFLAGS += -L$(ONNXRUNTIME_ROOT)/lib
endif
LDFLAGS += -lonnxruntime

# System libs
LDFLAGS += -lpthread -lrt -lm -lpcre

SRCS-y := main.c parser.c classifier.c flow_table.c stats.c \
          ml_classifier.c rpc_server.c
OBJS-y := $(SRCS-y:.c=.o)

TARGET = dpdk_traffic_engine

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS-y)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS-y) $(TARGET)
