#!/usr/bin/env bash
set -euo pipefail

# >>> FIX 1: INCREASE STACK SIZE TO PREVENT SEGFAULT <<<
ulimit -s unlimited
export OMP_STACKSIZE=128M
# ------------------------------------------------------

# Usage: ./run_fhe.sh [dataset] [model]
DATASET="${1:-mnist}"
MODEL="${2:-mlp}"

# SSH Configuration
REMOTE_HOST="${TD_HOST:-host.docker.internal}"
REMOTE_PORT="${TD_PORT:-10022}"
REMOTE_USER="${TD_USER:-root}"
REMOTE_PASS="${TD_PASS:-123456}"

TARGET="$REMOTE_USER@$REMOTE_HOST"

# --- SSH SETUP ---
mkdir -p ~/.ssh
cat <<EOF > ~/.ssh/config
Host *
    ControlMaster auto
    ControlPath ~/.ssh/cm-%r@%h:%p
    ControlPersist 10m
EOF
chmod 600 ~/.ssh/config

SSH_CMD="sshpass -p $REMOTE_PASS ssh -p $REMOTE_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -q"
SCP_CMD="sshpass -p $REMOTE_PASS scp -P $REMOTE_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -q"

echo "=================================================="
echo ">>> RUNNING REAL TDX BENCHMARK (FHE)"
echo ">>> Dataset: $DATASET | Model: $MODEL"
echo ">>> Target:  $TARGET"
echo "=================================================="

# Check TDX Status
echo "[Check] Verifying TDX Guest Status:"
$SSH_CMD "$TARGET" "dmesg | grep -i tdx | head -n 1" || echo "No TDX dmesg logs found."
echo "--------------------------------------------------"

# Clean Local
rm -f /data/W.csv /data/W1.csv /data/W2.csv /data/C1.bin /data/C2.bin

# 1) Host1: Train & Encrypt
echo "[Host] Phase 1: Training & KeyGen..."
cd host1
./host1-run "$DATASET" "$MODEL"
cd ..

# 2) Upload to TDX VM
echo "[Network] Uploading payload to TDX VM..."
$SSH_CMD "$TARGET" "mkdir -p /data/lib"
$SSH_CMD "$TARGET" "rm -f /data/cc.json /data/pk.bin /data/sk.bin" # Safety clean

$SCP_CMD /app/td-bootstrap "$TARGET":/data/
echo "[Network] Uploading OpenFHE libraries..."
$SCP_CMD /usr/local/openfhe/lib/libOPENFHE*.so* "$TARGET":/data/lib/
echo "[Network] Uploading Keys..."
$SCP_CMD /data/keys/cc.json /data/keys/pk.bin /data/keys/sk.bin "$TARGET":/data/

# 3) Host2: Run Inference
echo "[Host] Phase 2: Host2 Inference (with Remote TD Refresh)..."
$SSH_CMD -fN "$TARGET"

cd host2
./host2-run
cd ..

$SSH_CMD -O exit "$TARGET" 2>/dev/null || true

# 4) Host1: Decrypt
echo "[Host] Phase 3: Decryption..."
cd host1
RESULT=$(./host1-decrypt /data/C2.bin /data/preds.txt)
echo "$RESULT"

ACC=$(echo "$RESULT" | grep -oP 'Accuracy = \K[\d.]+')
echo "Accuracy: $ACC%"
