#!/bin/sh
set -eu

echo "[entrypoint] Bootstrapping TD VM…"


#THIS NEEDS TO BE CHANGED

: "${TD_HOST:=host.docker.internal}"
: "${TD_PORT:=10022}"
: "${TD_DIR:=/root/Benchmark/TDX_only_public}"

# Add the TD VM host key so strict checking works non-interactively
ssh-keyscan -p "$TD_PORT" "$TD_HOST" > /app/known_hosts 2>/dev/null || true

# Build td-init/td-run remotely and fetch td_pub.pem
/app/remote_bootstrap.sh

echo "[entrypoint] Starting host workload…"
exec /app/host

