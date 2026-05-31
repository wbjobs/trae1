CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include -D_GNU_SOURCE
LDFLAGS = -lpthread -lrt -lm -ldl -lm

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_NAME = nvme-hotplug

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/cli.c \
          $(SRC_DIR)/device.c \
          $(SRC_DIR)/uevent.c \
          $(SRC_DIR)/spdk_manager.c \
          $(SRC_DIR)/filesystem.c \
          $(SRC_DIR)/mount.c \
          $(SRC_DIR)/raid.c \
          $(SRC_DIR)/smart.c \
          $(SRC_DIR)/health.c \
          $(SRC_DIR)/logging.c \
          $(SRC_DIR)/io_queue.c \
          $(SRC_DIR)/removal_detection.c \
          $(SRC_DIR)/recovery.c \
          $(SRC_DIR)/prediction.c \
          $(SRC_DIR)/migration.c

OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

all: $(BUILD_DIR) $(BIN_NAME)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_NAME): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "Build complete: $(BIN_NAME)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_NAME)

install: $(BIN_NAME)
	install -D -m 0755 $(BIN_NAME) /usr/local/bin/$(BIN_NAME)
	install -D -m 0644 nvme-hotplug.conf /etc/nvme-hotplug.conf 2>/dev/null || true
	mkdir -p /var/lib/nvme-hotplug
	mkdir -p /var/log/nvme-hotplug

uninstall:
	rm -f /usr/local/bin/$(BIN_NAME)
	rm -f /etc/nvme-hotplug.conf
	rm -rf /var/lib/nvme-hotplug
	rm -rf /var/log/nvme-hotplug

.PHONY: all clean install uninstall

help:
	@echo "NVMe Hot-plug Management CLI Tool"
	@echo ""
	@echo "Build targets:"
	@echo "  all       - Build the nvme-hotplug binary (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install binary and config"
	@echo "  uninstall - Remove installed files"
	@echo ""
	@echo "Usage:"
	@echo "  $(BIN_NAME) list"
	@echo "  $(BIN_NAME) monitor [-d]"
	@echo "  $(BIN_NAME) add <pci_addr>"
	@echo "  $(BIN_NAME) remove <pci_addr>"
	@echo "  $(BIN_NAME) format <pci_addr> <ext4|xfs>"
	@echo "  $(BIN_NAME) mount <pci_addr> <path>"
	@echo "  $(BIN_NAME) unmount <pci_addr>"
	@echo "  $(BIN_NAME) raid-create <name> <raid0|raid1> <pci_addrs...>"
	@echo "  $(BIN_NAME) raid-destroy <name>"
	@echo "  $(BIN_NAME) smart-export <pci_addr> <file>"
	@echo "  $(BIN_NAME) predict <pci_addr> (ML-based failure prediction)"
	@echo "  $(BIN_NAME) migrate <source_pci> <target_pci> (online data migration)"
	@echo "  $(BIN_NAME) force-recover <pci_addr> (WARNING: may cause data loss)"
