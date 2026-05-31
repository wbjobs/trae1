FROM rust:1.75 as builder

WORKDIR /app

COPY Cargo.toml Cargo.lock ./
COPY src ./src

RUN cargo build --release

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/target/release/esni-gateway /usr/local/bin/
COPY block_list.txt /etc/esni-gateway/
COPY allow_list.txt /etc/esni-gateway/

EXPOSE 443 9090

CMD ["esni-gateway", "--listen", "0.0.0.0:443", "--metrics-addr", "0.0.0.0:9090"]
