FROM golang:1.22-bookworm AS bpf
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang llvm linux-headers-amd64 make ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY go.mod go.sum* ./
RUN go mod download || true
COPY . .
RUN make bpf

FROM golang:1.22-bookworm AS build
WORKDIR /src
COPY --from=bpf /src .
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /out/k8s-netpol-audit .

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates iproute2 && rm -rf /var/lib/apt/lists/*
COPY --from=build /out/k8s-netpol-audit /usr/local/bin/k8s-netpol-audit
ENTRYPOINT ["/usr/local/bin/k8s-netpol-audit"]
