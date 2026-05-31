FROM golang:1.21-alpine AS builder

RUN apk add --no-cache git ca-certificates

WORKDIR /app

COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o /sandbox-executor ./cmd/server

FROM gvisor/runsc:latest

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    ffmpeg \
    imagemagick \
    poppler-utils \
    ghostscript \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /sandbox/input /sandbox/output /sandbox/work /var/log/sandbox

COPY --from=builder /sandbox-executor /usr/local/bin/sandbox-executor

RUN chmod +x /usr/local/bin/sandbox-executor

ENV RUNSC_BIN_PATH=/usr/local/bin/runsc
ENV SANDBOX_ROOT_DIR=/sandbox
ENV SANDBOX_INPUT_DIR=/sandbox/input
ENV SANDBOX_OUTPUT_DIR=/sandbox/output
ENV SANDBOX_WORK_DIR=/sandbox/work
ENV SERVER_PORT=8080
ENV RESOURCE_CPU_LIMIT=1.0
ENV RESOURCE_MEMORY_LIMIT=536870912
ENV RESOURCE_PID_LIMIT=100
ENV RESOURCE_OOM_SCORE_ADJ=1000
ENV RESOURCE_IO_READ_LIMIT=10485760
ENV RESOURCE_IO_WRITE_LIMIT=10485760
ENV RESOURCE_NETWORK_MODE=none
ENV SANDBOX_MAX_CONCURRENT=10
ENV SANDBOX_DEFAULT_TIMEOUT=30s
ENV AI_SECURITY_ENABLED=true
ENV AI_SECURITY_OLLAMA_URL=http://host.docker.internal:11434
ENV AI_SECURITY_MODEL=qwen2.5-coder:7b-instruct
ENV AI_SECURITY_REQUEST_TIMEOUT=30s
ENV AI_SECURITY_CACHE_TTL=1h
ENV AI_SECURITY_REJECT_THRESHOLD=60
ENV AI_SECURITY_APPROVE_THRESHOLD=90
ENV AI_SECURITY_APPROVAL_WEBHOOK_URL=
ENV AI_SECURITY_APPROVAL_TIMEOUT=5m
ENV LOG_LEVEL=info
ENV LOG_OUTPUT_PATH=/var/log/sandbox

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

ENTRYPOINT ["/usr/local/bin/sandbox-executor"]
