#!/bin/sh
set -e

echo "Waiting for TD to generate keypair…"
# block until td-init has written the public key
while [ ! -f /data/td_pub.pem ]; do
  sleep 0.5
done

echo "Found td_pub.pem, starting host benchmark"
exec /app/host

