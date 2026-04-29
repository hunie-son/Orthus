#!/bin/bash

# ==============================================================================
# AUTOMATED FHE BENCHMARK SUITE (BM2 - FHE Only)
# ==============================================================================

# --- Configuration ---
# 16 threads might be too aggressive for pure FHE if not optimized, 
# but we will keep your list.

THREADS_LIST=(1 2 4 6 8 10 12 14 16)
RUNS_PER_CONFIG=5
DATASETS=("iris" "wdbc" "mnist")
MODELS=("lr" "mlp")

# Output Directory
OUTPUT_DIR="bm2_results_updated"
mkdir -p "$OUTPUT_DIR"

echo "=================================================================="
echo "Starting Benchmark Suite (BM2)"
echo "Output Directory: $OUTPUT_DIR/"
echo "File Format: {dataset}_{model}.csv (Aggregated)"
echo "=================================================================="

# 1. Smart Build
echo "[Setup] Checking build status..."
# Ensure we build the images defined in the current directory's docker-compose.yml
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
        if [ ! -f "$CSV_FILE" ]; then
            echo "dataset,model,threads,iteration,avg_latency_ms,train_acc_percent,test_acc_percent,fhe_acc_percent" > "$CSV_FILE"
        fi

        echo "------------------------------------------------------------------"
        echo ">>> Processing: Dataset=$dataset | Model=$model"
        echo "    Output: $CSV_FILE"
        echo "------------------------------------------------------------------"

        for threads in "${THREADS_LIST[@]}"; do
            
            for (( i=1; i<=RUNS_PER_CONFIG; i++ )); do
                
                # --- RESUME CHECK ---
                if grep -q ",$threads,$i," "$CSV_FILE"; then
                    echo "    [Skip] Config: $threads threads | Run: $i (Found in CSV)"
                    continue
                fi

                echo -n "    [Run]  Config: $threads threads | Run: $i... "
                
                # --- EXECUTION COMMAND (BM2) ---
                # NOTE: In BM2, the service name is usually 'host' or 'fhe_host'.
                # Checking your previous logs, it's likely 'host' based on the folder name.
                # If your docker-compose.yml uses a different service name, change 'host' below.
                #OUTPUT=$(docker compose run -e OMP_NUM_THREADS=$threads --rm host ./run_fhe.sh "$dataset" "$model" 2>&1)
		OUTPUT=$(docker compose run \
    			-e OMP_NUM_THREADS=$threads \
    			-e BATCH_CAP=100 \
    			--rm host ./run_fhe.sh "$dataset" "$model" 2>&1)		
                
		# Check for Docker failure
                if [ $? -ne 0 ]; then
                    echo "FAILED"
                    echo "$dataset,$model,$threads,$i,ERROR,0,0,0" >> "$CSV_FILE"
                    continue
                fi

                # --- Parse Results ---
                # These grep patterns assume BM2 prints the same "Avg Latency" and "Accuracy" logs.
                # If BM2 prints "Total Time" instead of Latency, we might need to adjust.
                
                #LATENCY=$(echo "$OUTPUT" | grep -oP "Avg Latency: \K[\d.]+")
                LATENCY=$(echo "$OUTPUT" | grep -oP "Avg Latency \(ms/sample\):\s*\K[\d.]+" | tail -n 1)

		if [[ -z "$LATENCY" ]]; then
			LATENCY=$(echo "$OUTPUT" | grep -oP "Avg Latency:\s*\K[\d.]+" | tail -n 1)
		fi
		
		if [[ -z "$LATENCY" ]]; then
                     # Fallback: Sometimes BM2 might log "Total Time" or similar. 
                     # Checking for common variations if Avg Latency misses.
                     LATENCY=$(echo "$OUTPUT" | grep -oP "Total Latency: \K[\d.]+")
                fi
                
                TRAIN_ACC=$(echo "$OUTPUT" | grep -oP "Train acc: \K[\d.]+")
                TEST_ACC=$(echo "$OUTPUT" | grep -oP "Test acc: \K[\d.]+")
                FHE_ACC=$(echo "$OUTPUT" | grep -oP "Accuracy: \K[\d.]+")

                # Default to NaN if missing
                LATENCY=${LATENCY:-NaN}
                TRAIN_ACC=${TRAIN_ACC:-NaN}
                TEST_ACC=${TEST_ACC:-NaN}
                FHE_ACC=${FHE_ACC:-NaN}

                echo "Done. (Lat: ${LATENCY}ms | Acc: ${FHE_ACC}%)"

                # Append to CSV
                echo "$dataset,$model,$threads,$i,$LATENCY,$TRAIN_ACC,$TEST_ACC,$FHE_ACC" >> "$CSV_FILE"
                
                sleep 1
            done
        done
    done
done

echo "=================================================================="
echo "Cleaning up Docker resources..."
docker compose down
echo "Benchmark Suite Finished Successfully."
echo "=================================================================="
