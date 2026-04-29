#!/bin/bash

# ==============================================================================
# AUTOMATED BENCHMARK SUITE (BM1 - TDX Only)
# ==============================================================================

# No THREADS_LIST needed for BM1 since it's standard TDX execution without FHE threading.

RUNS_PER_CONFIG=10
DATASETS=("iris" "wdbc" "mnist")
MODELS=("lr" "mlp")

# Output Directory
OUTPUT_DIR="bm1_results"
mkdir -p "$OUTPUT_DIR"

echo "=================================================================="
echo "Starting Benchmark Suite (BM1)"
echo "Output Directory: $OUTPUT_DIR/"
echo "File Format: {dataset}_{model}.csv (Aggregated)"
echo "=================================================================="

# 1. Build
echo "[Setup] Checking build status..."
docker compose build > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "Error: Docker build failed. Exiting."
    exit 1
fi
echo "[Setup] Ready."

# 2. Main Execution Loop
for dataset in "${DATASETS[@]}"; do
    for model in "${MODELS[@]}"; do
        
        # ONE CSV file per Dataset/Model pair
        CSV_FILE="${OUTPUT_DIR}/${dataset}_${model}.csv"
        
        # Create file and write header if it doesn't exist
        # Note: Changed to avg_latency_us since BM1 measures in microseconds
        if [ ! -f "$CSV_FILE" ]; then
            echo "dataset,model,iteration,avg_latency_us,train_acc_percent,test_acc_percent,tdx_acc_percent" > "$CSV_FILE"
        fi

        echo "------------------------------------------------------------------"
        echo ">>> Processing: Dataset=$dataset | Model=$model"
        echo "    Output: $CSV_FILE"
        echo "------------------------------------------------------------------"

        for (( i=1; i<=RUNS_PER_CONFIG; i++ )); do
            
            # --- RESUME CHECK ---
            # Checks if this exact run (dataset, model, iteration) is already logged
            if grep -q "^$dataset,$model,$i," "$CSV_FILE"; then
                echo "    [Skip] Run: $i (Found in CSV)"
                continue
            fi

            echo -n "    [Run]  Run: $i... "
            
            # --- EXECUTION COMMAND (BM1) ---
            # Calls run_bm1.sh instead of run_fhe.sh, no OMP environment variables needed
            OUTPUT=$(docker compose run --rm host ./run_bm1.sh "$dataset" "$model" 2>&1)
            
            # Check for Docker failure
            if [ $? -ne 0 ]; then
                echo "FAILED"
                echo "$dataset,$model,$i,ERROR,0,0,0" >> "$CSV_FILE"
                continue
            fi

            # --- Parse Results ---
            LATENCY=$(echo "$OUTPUT" | grep -oP "Avg Latency: \K[\d.]+")
            TRAIN_ACC=$(echo "$OUTPUT" | grep -oP "Train acc: \K[\d.]+")
            TEST_ACC=$(echo "$OUTPUT" | grep -oP "Test acc: \K[\d.]+")
            TDX_ACC=$(echo "$OUTPUT" | grep -oP "Accuracy: \K[\d.]+")

            # Default to NaN if missing
            LATENCY=${LATENCY:-NaN}
            TRAIN_ACC=${TRAIN_ACC:-NaN}
            TEST_ACC=${TEST_ACC:-NaN}
            TDX_ACC=${TDX_ACC:-NaN}

            echo "Done. (Lat: ${LATENCY}us | Acc: ${TDX_ACC}%)"

            # Append to CSV
            echo "$dataset,$model,$i,$LATENCY,$TRAIN_ACC,$TEST_ACC,$TDX_ACC" >> "$CSV_FILE"
            
            sleep 1
        done
    done
done

echo "=================================================================="
echo "Cleaning up Docker resources..."
docker compose down
echo "Benchmark Suite Finished Successfully."
echo "=================================================================="
