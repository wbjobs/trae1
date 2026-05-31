# K8s NetworkPolicy Audit Tool
# Builds: eBPF (BCC/clang) + Go binary

BIN      := k8s-netpol-audit
BPF_SRC  := bpf/flow.c
BPF_OBJ  := bpf_bpfel.o
BPF_HDRS := -I./bpf
BPF_CC   ?= clang
BPF_CFLAGS := -O2 -g -Wall -target bpf $(BPF_HDRS)

.PHONY: all bpf build tidy clean run docker

all: bpf build

bpf: $(BPF_OBJ)

$(BPF_OBJ): $(BPF_SRC) bpf/vmlinux.h
	@mkdir -p $(dir $@)
	$(BPF_CC) $(BPF_CFLAGS) -c $< -o $@
	llvm-strip -g $@ || true

build:
	CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o $(BIN) .

tidy:
	go mod tidy

run: all
	sudo ./$(BIN)

clean:
	rm -f $(BPF_OBJ) $(BIN)

# Optional: regenerate vmlinux.h from running kernel BTF
vmlinux:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > bpf/vmlinux.h

docker:
	docker build -t k8s-netpol-audit:latest .
