.PHONY: all build clean test install fmt vet

APP_NAME := io-qos
MAIN_PKG := ./cmd/io-qos
BUILD_DIR := bin

all: build

build:
	@echo "Building $(APP_NAME)..."
	@mkdir -p $(BUILD_DIR)
	go build -ldflags="-s -w" -o $(BUILD_DIR)/$(APP_NAME) $(MAIN_PKG)
	@echo "Build complete: $(BUILD_DIR)/$(APP_NAME)"

install:
	@echo "Installing $(APP_NAME) to /usr/local/bin..."
	go install -ldflags="-s -w" $(MAIN_PKG)

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)
	@rm -rf .io-qos-rollback

fmt:
	@echo "Formatting code..."
	go fmt ./...

vet:
	@echo "Running go vet..."
	go vet ./...

test:
	@echo "Running tests..."
	go test -v ./...

deps:
	@echo "Downloading dependencies..."
	go mod download
	go mod tidy

check: fmt vet test

build-linux:
	@echo "Building Linux binary..."
	@mkdir -p $(BUILD_DIR)
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags="-s -w" -o $(BUILD_DIR)/$(APP_NAME)-linux-amd64 $(MAIN_PKG)

build-arm:
	@echo "Building ARM64 binary..."
	@mkdir -p $(BUILD_DIR)
	CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -ldflags="-s -w" -o $(BUILD_DIR)/$(APP_NAME)-linux-arm64 $(MAIN_PKG)

release: build-linux build-arm
	@echo "Release builds complete"
	@ls -la $(BUILD_DIR)/
