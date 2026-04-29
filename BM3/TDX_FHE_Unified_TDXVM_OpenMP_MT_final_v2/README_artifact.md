# BM3 – TDX-Assisted FHE Unified Benchmark  
**Hybrid Confidential Inference with Host-Side OpenFHE and Refresh inside a Real Intel TDX VM**

This repository implements **Benchmark 3 (BM3)**, the **hybrid TEE + FHE benchmark** in the artifact evaluation benchmark suite.

BM3 combines **host-side CKKS inference with OpenFHE** and **ciphertext refresh inside a real Intel TDX guest VM**. Most encrypted computation stays on the host, while refresh operations are offloaded to a protected TDX guest through a decrypt -> re-encode -> re-encrypt workflow.

BM3 serves as the **hybrid design point** between:

- **BM1**, which measures TDX-only plaintext inference inside the guest, and
- **BM2**, which performs pure host-side OpenFHE bootstrapped inference.

---

## Benchmark Scope

BM3 evaluates the following configurations:

### Datasets
- **Iris**
- **WDBC (Breast Cancer Diagnostic)**
- **MNIST**

### Models
- **Logistic Regression (LR)**
- **One-hidden-layer MLP**

### Experimental Parameters
- **Metric**: Average per-sample encrypted inference latency  
- **Unit**: milliseconds (ms)
- **Thread counts**: 1, 2, 4, 6, 8, 10, 12, 14, 16
- **Repetitions**: 5 runs per dataset/model/thread setting

### Total Configurations
- 3 datasets
- 2 models
- 9 thread counts
- 5 repetitions

That gives **270 benchmark runs** in the full BM3 sweep.

---

## What BM3 Measures (and What It Does Not)

### ✅ Included
- Model training in cleartext on the host
- CKKS key generation and encrypted packing on the host
- Host-side encrypted LR or MLP inference
- OpenMP-based parallel execution
- Remote ciphertext refresh inside a **real TDX VM**
- Final local decryption and encrypted-accuracy measurement
- Network transfer of refresh requests and responses via SSH/SCP

### ❌ Not Included
- OpenFHE `EvalBootstrap` in the active inference path
- Remote attestation
- Production-grade key provisioning
- Mutual authentication beyond password-based SSH
- A fully self-contained production TDX service stack
- End-to-end attested key exchange

BM3 intentionally isolates the behavior of a **TEE-assisted ciphertext refresh pipeline**.

---

## System Architecture

```text
Host (Docker Container)
 ├─ Host1
 │   ├─ Load and normalize data
 │   ├─ Train LR / MLP in clear
 │   ├─ Generate CKKS context and keys
 │   ├─ Pack and encrypt test samples
 │   └─ Serialize encrypted inputs and metadata
 │
 ├─ Host2
 │   ├─ Load encrypted inputs and model weights
 │   ├─ Run encrypted inference with OpenMP
 │   ├─ Invoke td_refresh() when refresh is needed
 │   ├─ Send ciphertexts to the TDX guest
 │   └─ Write encrypted class scores
 │
 └─ Host1-decrypt
     ├─ Decrypt encrypted outputs
     ├─ Recover predictions
     ├─ Compare with true labels
     └─ Write CSV output

Remote TDX Guest VM
 ├─ Receive encrypted refresh request
 ├─ Decrypt ciphertext using CKKS secret key
 ├─ Re-encode plaintext at a fresh level
 ├─ Re-encrypt using CKKS public key
 └─ Return refreshed ciphertext
```

---

## Repository Structure

```text
TDX_FHE_Unified_TDXVM_OpenMP_MT_final_v2/
├── auto_bm.sh                   # Full benchmark driver
├── docker-compose.yml           # Host container definition
├── docker_cleanup.sh
├── bm3_results/
├── bm3_results_update/          # Aggregated CSV results used by current script
│   ├── iris_lr.csv
│   ├── iris_mlp.csv
│   ├── wdbc_lr.csv
│   ├── wdbc_mlp.csv
│   ├── mnist_lr.csv
│   └── mnist_mlp.csv
├── data/                        # Shared runtime artifacts and datasets
│   ├── iris_new.csv
│   ├── wdbc.csv
│   ├── mnist_train.csv
│   ├── mnist_test.csv
│   ├── pack_meta.txt
│   ├── C1.bin
│   ├── C2.bin
│   ├── td-bootstrap
│   └── ...
├── host/
│   ├── Dockerfile
│   ├── run_fhe.sh               # Host1 → Host2 → Host1-decrypt orchestration
│   ├── run_fhe_original.sh
│   ├── host1/
│   │   ├── main.cpp             # training + CKKS setup + packing + encryption
│   │   ├── host1-decrypt.cpp    # final decryption and accuracy recovery
│   │   ├── model.*              # LR + MLP implementation
│   │   ├── data.*               # dataset loading & normalization
│   │   └── Makefile
│   └── host2/
│       ├── host2-run.cpp        # encrypted inference + td_refresh calls
│       ├── utils.hpp            # polynomial sigmoid and helpers
│       └── Makefile
└── td/
    ├── Dockerfile
    ├── Makefile
    └── src/
        ├── td-bootstrap.cpp     # TDX-side refresh binary
        ├── td-bootstrap-init
        └── utils.hpp
```

---

## Execution Workflow

### 1. Build the Host Container

```bash
docker compose build
```

### 2. Run the Full Benchmark Suite

```bash
chmod +x auto_bm.sh host/run_fhe.sh docker_cleanup.sh
./auto_bm.sh
```

This will:

- iterate over all dataset/model combinations
- sweep all OpenMP thread counts
- execute each configuration **5 times**
- append results into CSV files under `bm3_results_update/`

### 3. Optional: Single Configuration Run

```bash
docker compose run -e OMP_NUM_THREADS=8 -e BATCH_CAP=100 --rm host ./run_fhe.sh mnist mlp
```

---

## End-to-End Benchmark Steps

1. **Host-side training**
   - LR or MLP is trained on cleartext data
   - Train/Test accuracy is reported

2. **CKKS setup and packing**
   - Host1 generates the CKKS context and keys
   - evaluation keys are serialized
   - test samples are packed and encrypted into ciphertext batches

3. **Host-side encrypted inference**
   - Host2 loads encrypted inputs and model weights
   - performs encrypted LR or MLP forward computation
   - uses OpenMP to parallelize classes or hidden units

4. **TDX-assisted refresh**
   - ciphertexts are serialized into refresh request files
   - sent to the real TDX guest via `scp`
   - refreshed inside the guest by `td-bootstrap`
   - returned to the host and reinserted into the inference path

5. **Decryption and evaluation**
   - Host1 decrypts `/data/C2.bin`
   - reconstructs per-sample predictions
   - computes final encrypted accuracy

6. **CSV aggregation**
   ```csv
   dataset,model,threads,iteration,avg_latency_ms,train_acc_percent,test_acc_percent,fhe_acc_percent
   ```

---

## Dataset Configuration

| Dataset | Train Size | Test Size | Notes               |
| ------- | ---------: | --------: | ------------------- |
| Iris    |      ~120 |       ~30 | 80/20 split         |
| WDBC    |      ~455 |      ~114 | Normalized          |
| MNIST   |       5000 |        48 | Scaled pixel values |

---

## Model Hyperparameters

| Dataset | Model | Hidden Units | Epochs | LR |
| ------- | ----- | -----------: | -----: | --: |
| Iris    | MLP   |           10 |    200 | 0.10 |
| WDBC    | MLP   |           16 |    200 | 0.10 |
| MNIST   | MLP   |           64 |     50 | 0.15 |
| All     | LR    |            – | dataset dependent | same |

Additional BM3 runtime settings:

- **Ring dimension**: 65536
- **Security level**: `HEStd_128_classic`
- **Scaling mod size**: 59
- **Multiplicative depth**: 10
- **Packing cap**: default `BATCH_CAP=100`
- **Refresh path**: remote TDX decrypt -> re-encode -> re-encrypt

---

## Experimental Results (Summary)

Best observed mean latency across tested thread settings:

| Dataset | Model | Best Threads | Best Avg Latency (ms) | BM3 Acc (%) |
| ------- | ----- | -----------: | --------------------: | ----------: |
| Iris    | LR    |            6 |              ~238.71 |      100.00 |
| Iris    | MLP   |           10 |             ~1209.01 |       80.00 |
| WDBC    | LR    |            8 |               ~61.12 |      100.00 |
| WDBC    | MLP   |           14 |              ~995.55 |      100.00 |
| MNIST   | LR    |            8 |             ~1507.05 |       89.58 |
| MNIST   | MLP   |            4 |            ~14114.28 |       93.75 |

Representative observations:

- BM3 is **substantially faster than BM2** because it replaces host-side bootstrapping with TDX-assisted refresh.
- LR preserves encrypted accuracy across all three datasets.
- WDBC MLP and MNIST MLP preserve host-side test accuracy under the BM3 pipeline.
- **Iris MLP is the main outlier**, where encrypted accuracy drops to 80% despite 100% host test accuracy.
- Thread scaling helps, but refresh remains partially serialized, so speedup is not perfectly linear.

---

## Security Notes & Limitations

- Refresh uses **decrypt and re-encrypt inside TDX**, not cryptographic bootstrapping
- SSH uses password-based authentication via `sshpass`
- No remote attestation is performed
- CKKS keys are uploaded to the TDX guest as serialized files
- Refresh traffic is network serialized through one protected path
- This is a **functional research benchmark**, not a production hybrid confidential-computing service

---

## Purpose of BM3

BM3 establishes the **hybrid TEE + FHE confidential inference benchmark**.

It answers:

- How much latency can be reduced when ciphertext refresh is offloaded to TDX?
- How does hybrid refresh compare with BM2’s pure FHE bootstrapping baseline?
- Which workloads preserve accuracy under TDX-assisted refresh?
- How much parallel speedup remains when refresh is partially serialized over the network?

BM3 intentionally avoids:

- production attestation and provisioning complexity
- a fully independent TDX orchestration stack
- pure OpenFHE bootstrapping in the active path

Those tradeoffs are deliberate for artifact evaluation.

---

## Citation

If referenced in an artifact evaluation:

> **BM3** is a hybrid TEE + FHE benchmark that measures per-sample encrypted inference latency and accuracy for LR and MLP models using host-side OpenFHE CKKS computation with ciphertext refresh delegated to a real Intel TDX guest VM.

