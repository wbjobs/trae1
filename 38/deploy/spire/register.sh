#!/bin/sh
set -e
SERVER="spire-server"
DOMAIN="example.org"

# Trust domain bootstrap
echo "Bootstrapping trust domain ${DOMAIN}..."
docker compose exec -T spire-server \
  /opt/spire/bin/spire-server trustdomain create \
  -trustDomain ${DOMAIN}

# Create a join token for the agent
TOKEN=$(docker compose exec -T spire-server \
  /opt/spire/bin/spire-server token generate -spiffeID spiffe://${DOMAIN}/node | awk '{print $2}')
echo "Generated join token: ${TOKEN}"

# Register the gateway workload (spiffe://example.org/ns/svc/gateway)
docker compose exec -T spire-server \
  /opt/spire/bin/spire-server entry create \
  -parentID spiffe://${DOMAIN}/node \
  -spiffeID spiffe://${DOMAIN}/ns/svc/gateway \
  -selector unix:uid:0

# Register additional services as needed
docker compose exec -T spire-server \
  /opt/spire/bin/spire-server entry create \
  -parentID spiffe://${DOMAIN}/node \
  -spiffeID spiffe://${DOMAIN}/ns/svc/user-api \
  -selector unix:uid:0

docker compose exec -T spire-server \
  /opt/spire/bin/spire-server entry create \
  -parentID spiffe://${DOMAIN}/node \
  -spiffeID spiffe://${DOMAIN}/ns/svc/client \
  -selector unix:uid:0

echo "SPIFFE entries registered"
