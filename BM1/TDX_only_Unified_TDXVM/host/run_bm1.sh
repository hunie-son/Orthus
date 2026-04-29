#!/usr/bin/env bash
set -euo pipefail

# Args: [dataset] [model]
DATASET="${1:-iris}"
MODEL="${2:-mlp}"

# SSH Config
# TDX Support needed it
REMOTE_HOST="${TD_SSH_HOST:-host.docker.internal}"
REMOTE_PORT="${TD_SSH_PORT:-10022}"
REMOTE_USER="${TD_SSH_USER:-root}"
REMOTE_PASS="123456"  # Default TDX VM password

TARGET="$REMOTE_USER@$REMOTE_HOST"

# SSH/SCP Commands wrapped with sshpass
# -o StrictHostKeyChecking=no : Don't ask for fingerprint confirmation
# -q : Quiet mode
SSH_CMD="sshpass -p $REMOTE_PASS ssh -p $REMOTE_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -q"
SCP_CMD="sshpass -p $REMOTE_PASS scp -P $REMOTE_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -q"

echo "=================================================="
echo ">>> RUNNING REAL TDX BENCHMARK (Password Auth)"
echo ">>> Dataset: $DATASET | Model: $MODEL"
echo ">>> Target:  $TARGET"
echo "=================================================="

# 1) Clean Local Data
rm -f /data/W.csv /data/W1.csv /data/W2.csv /data/C1.bin /data/C2.bin /data/Cres.bin /data/config.txt /data/td_priv.pem /data/td_pub.pem

# 2) Key Generation (Local)
echo "[System] Generating RSA Keypair..."
openssl genrsa -out /data/td_priv.pem 2048 2>/dev/null
openssl rsa -in /data/td_priv.pem -pubout -out /data/td_pub.pem 2>/dev/null

# 3) Host: Train & Encrypt
echo "[Host] Phase 1: Training & Encryption..."
./host "$DATASET" "$MODEL" "encrypt"

# 4) Network: Upload to TDX VM
echo "[Network] Uploading payload to TDX VM..."

# Ensure /data exists on remote
$SSH_CMD "$TARGET" "mkdir -p /data"

# Upload Binary & Data
$SCP_CMD /app/td-run "$TARGET":/data/
$SCP_CMD /data/C1.bin /data/C2.bin /data/config.txt /data/td_priv.pem "$TARGET":/data/
$SCP_CMD /data/W1.csv "$TARGET":/data/
if [ -f /data/W2.csv ]; then
    $SCP_CMD /data/W2.csv "$TARGET":/data/
fi

# 5) Network: Execute Inference
echo "[Network] Executing Inference on Real Hardware..."

# --------------------
echo "[Check] Verifying TDX Guest Status:"
$SSH_CMD "$TARGET" "dmesg | grep -i tdx | head -n 3" || echo "No TDX dmesg logs found."
echo "--------------------------------------------------"
# ------------------------

# We explicitly allow the command to take time
$SSH_CMD "$TARGET" "chmod +x /data/td-run && /data/td-run"

# 6) Network: Download Results
echo "[Network] Downloading results..."
$SCP_CMD "$TARGET":/data/Cres.bin /data/Cres.bin

# 7) Host: Decrypt
echo "[Host] Phase 3: Decryption..."
./host "$DATASET" "$MODEL" "decrypt"
