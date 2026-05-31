FROM golang:1.22-alpine AS builder
WORKDIR /src
COPY go.mod go.sum* ./
RUN go mod download || true
COPY . .
RUN CGO_ENABLED=0 go build -ldflags="-s -w" -o /svid-gateway ./cmd/svid-gateway
RUN CGO_ENABLED=0 go build -ldflags="-s -w" -o /svidctl ./cmd/svidctl

FROM node:20-alpine AS web-builder
WORKDIR /src/web
COPY web/package.json ./
RUN npm install --no-audit --no-fund
COPY web/ ./
RUN npm run build

FROM alpine:3.20
RUN apk --no-cache add ca-certificates tzdata
COPY --from=builder /svid-gateway /usr/local/bin/svid-gateway
COPY --from=builder /svidctl /usr/local/bin/svidctl
COPY --from=web-builder /src/web/build /opt/svid-gateway/web/build
WORKDIR /opt/svid-gateway
EXPOSE 8080 8443
ENTRYPOINT ["/usr/local/bin/svid-gateway"]
