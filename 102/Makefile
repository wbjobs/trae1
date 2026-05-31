.PHONY: all clean build vmlinux perf-test perf-test-l7

CLANG := clang
CLANG_INCLUDE := -I/usr/include -I./bpf
CFLAGS := -O2 -target bpf -D__TARGET_ARCH_x86

all: build

vmlinux:
	./scripts/generate-vmlinux.sh

bpf: bpf/vmlinux.h
	cd pkg/bpf && go generate

build: bpf
	go build -o xdp-accel ./cmd/xdp-accel
	go build -o perf-test ./cmd/perf-test

perf-test: build
	./perf-test --perf-rules=1000 --perf-packets=10000 --perf-mode=mixed

perf-test-full: build
	./perf-test --perf-rules=5000 --perf-packets=100000 --perf-mode=mixed

perf-test-l7: build
	./xdp-accel --perf-test --perf-rules=5000 --enable-l7 --stats

bpf-l7: bpf/vmlinux.h
	$(CLANG) $(CFLAGS) $(CLANG_INCLUDE) -c bpf/xdp_k8s_accel_l7.c -o bpf/xdp_k8s_accel_l7.o

clean:
	rm -f xdp-accel
	rm -f perf-test
	rm -f pkg/bpf/bpf_*.go
	rm -f pkg/bpf/bpf_*.o
	rm -f bpf/vmlinux.h
	rm -f bpf/xdp_k8s_accel_opt.o
	rm -f bpf/xdp_k8s_accel_l7.o

test: build
	@echo "Running unit tests..."
	go test -v ./pkg/...

lint:
	golangci-lint run ./...

.PHONY: all clean build vmlinux perf-test perf-test-full perf-test-l7 bpf bpf-l7 test lint
