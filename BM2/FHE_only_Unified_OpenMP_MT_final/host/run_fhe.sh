#!/usr/bin/env bash
set -euo pipefail

# Usage: ./run_fhe.sh [dataset] [model]
DATASET="${1:-mnist}"
MODEL="${2:-mlp}"

echo "=================================================="
echo ">>> RUNNING BENCHMARK 2 (REAL FHE BOOTSTRAPPING)"
echo ">>> Dataset: $DATASET | Model: $MODEL"
echo "=================================================="

# --- CRITICAL: Clean Old Keys to force Regeneration ---
rm -rf /data/keys /data/W.csv /data/W1.csv /data/W2.csv /data/C1.bin /data/C2.bin /data/sk.bin /data/pack_meta.txt

# 1) Host1: Train & KeyGen
echo "[Host] Phase 1: Training, KeyGen (Slow) & Encryption..."
cd host1
./host1-run "$DATASET" "$MODEL"
cd ..

# 2) Host2: Inference
echo "[Host] Phase 2: Host2 Inference (EvalBootstrap)..."
cd host2
./host2-run
cd ..

# 3) Host1: Decrypt
echo "[Host] Phase 3: Decryption..."
cd host1
RESULT=$(./host1-decrypt /data/C2.bin /data/preds.txt)
echo "$RESULT"

# Extract accuracy
ACC=$(echo "$RESULT" | grep -oP 'Accuracy = \K[\d.]+')
echo "Accuracy: $ACC%"
