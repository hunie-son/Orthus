#!/bin/sh
set -e

REMOTE_HOST="${TD_HOST:-host.docker.internal}"
REMOTE="root@${REMOTE_HOST}"
PORT="10022"
RDIR="/root/Benchmark/TDX_only_public"

# Build a fresh, container-local known_hosts file for the TD host
KNOWN="/tmp/td_known_hosts"
rm -f "$KNOWN"

# Try a few times in case SSH isn't ready yet; grab ALL key types (rsa, ecdsa, ed25519)
for i in 1 2 3 4 5; do
  if ssh-keyscan -T 3 -p "${PORT}" -t rsa,ecdsa,ed25519 "${REMOTE_HOST}" >> "$KNOWN" 2>/dev/null; then
    break
  fi
  sleep 1
done
if [ ! -s "$KNOWN" ]; then
  echo "[bootstrap] ERROR: Could not fetch SSH host key for ${REMOTE_HOST}:${PORT}" >&2
  exit 1
fi

SSH="ssh -i /ssh/id_ed25519 -o BatchMode=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=${KNOWN} -p${PORT}"
SCP="scp -i /ssh/id_ed25519 -o BatchMode=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=${KNOWN} -P${PORT}"

echo "[bootstrap] Ensuring remote dir exists on ${REMOTE_HOST}…"
$SSH $REMOTE "mkdir -p ${RDIR}"

echo "[bootstrap] Installing deps if needed…"
$SSH $REMOTE "if ! dpkg -s build-essential >/dev/null 2>&1 || ! dpkg -s libssl-dev >/dev/null 2>&1; then \
  apt-get update && apt-get install -y build-essential libssl-dev; fi"

echo "[bootstrap] Uploading TD sources…"
$SCP /app/tdsrc/td-run.cpp /app/tdsrc/td-init.cpp /app/tdsrc/utils.hpp $REMOTE:${RDIR}/
if [ -f /app/tdsrc/td-run_relu.cpp ]; then
  $SCP /app/tdsrc/td-run_relu.cpp $REMOTE:${RDIR}/ || true
fi

echo "[bootstrap] Building td-init and td-run…"
#$SSH $REMOTE "set -e; cd ${RDIR} && g++ -std=c++17 -O2 -o td-init td-init.cpp -lcrypto && g++ -std=c++17 -O2 -o td-run td-run.cpp -lcrypto"

#To avoid the error messages 
$SSH $REMOTE "set -e; cd ${RDIR} && \
  g++ -std=c++17 -O2 -DOPENSSL_SUPPRESS_DEPRECATED -Wno-deprecated-declarations \
     -o td-init td-init.cpp -lcrypto && \
  g++ -std=c++17 -O2 -DOPENSSL_SUPPRESS_DEPRECATED -Wno-deprecated-declarations \
     -o td-run td-run.cpp  -lcrypto"



echo "[bootstrap] Generating keys if missing…"
$SSH $REMOTE "cd ${RDIR} && [ -f td_pub.pem ] && [ -f td_priv.pem ] || ./td-init"

echo "[bootstrap] Done."

