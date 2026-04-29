# BM1 – TDX-Only Unified Benchmark
**Confidential ML Inference inside a Real Intel TDX VM**

This repository implements **Benchmark 1 (BM1)**, a baseline benchmark designed for
**artifact evaluation of confidential machine learning inference** using
**Intel TDX (Trust Domain Extensions)**.

BM1 measures the **guest-side inference latency** of standard ML models running
inside a **real TDX-protected virtual machine**, without homomorphic encryption.
Encrypted transport is used between the host and the TDX guest to prevent plaintext
input and output exposure during transfer.

BM1 serves as a **clean baseline** before introducing more advanced designs
(e.g., TEE + FHE, ciphertext refresh, packed inference).

---

## Benchmark Scope

BM1 evaluates the following configurations:

### Datasets
- **Iris**
- **WDBC (Breast Cancer Diagnostic)**
- **MNIST**

### Models
- **Logistic Regression (LR)**
- **One-hidden-layer MLP**

### Experimental Parameters
- **Metric**: Average per-sample inference latency inside the TDX guest  
- **Unit**: microseconds (µs)
- **Repetitions**: 10 runs per dataset/model pair

### Total Configurations (6)
1. iris + lr
2. iris + mlp
3. wdbc + lr
4. wdbc + mlp
5. mnist + lr
6. mnist + mlp

---

## What BM1 Measures (and What It Does Not)

### ✅ Included
- Model training in cleartext on the host
- Encrypted payload transfer (RSA + AES)
- Inference execution inside a **real TDX VM**
- Guest-side per-sample inference latency
- Accuracy comparison between host and guest

### ❌ Not Included
- Homomorphic encryption (FHE)
- Ciphertext packing or refresh
- Remote attestation protocols
- Production-grade key provisioning
- Host↔guest round-trip timing

BM1 intentionally isolates **TDX-only inference cost**.

---

## System Architecture

```text
Host (Docker Container)
 ├─ Train LR / MLP in clear
 ├─ Encrypt test data
 ├─ Upload encrypted inputs + model
 │
 ▼
TDX Guest VM (Real Hardware)
 ├─ Decrypt payload
 ├─ Run inference
 ├─ Measure latency (µs/sample)
 ├─ Encrypt results
 │
 ▼
Host
 ├─ Decrypt results
 ├─ Aggregate latency and accuracy
 └─ Write CSV output
````

***

## Repository Structure

```text
TDX_only_Unified_TDXVM/
├── auto_bm1.sh                 # Full benchmark driver
├── docker-compose.yml          # Host container definition
├── docker_cleanup.sh
├── bm1_results/                # Aggregated CSV results
│   ├── iris_lr.csv
│   ├── iris_mlp.csv
│   ├── wdbc_lr.csv
│   ├── wdbc_mlp.csv
│   ├── mnist_lr.csv
│   └── mnist_mlp.csv
├── host/
│   ├── Dockerfile
│   ├── run_bm1.sh              # Host↔TDX orchestration
│   ├── data/                   # Datasets
│   └── src/
│       ├── host.cpp            # Train / encrypt / decrypt
│       ├── td-run.cpp          # Uploaded TDX inference binary
│       ├── model.*             # LR + MLP implementation
│       ├── data.*              # Dataset loading & normalization
│       ├── crypto.*            # RSA + AES utilities
│       └── utils.hpp
└── td/                         # Legacy/alternate TD container (inactive)
```

***

## Execution Workflow

### 1. Build the Host Container

```bash
docker compose build
```

### 2. Run the Full Benchmark Suite

```bash
chmod +x auto_bm1.sh host/run_bm1.sh docker_cleanup.sh
./auto_bm1.sh
```

This will:

*   Iterate over all dataset/model combinations
*   Execute each configuration **10 times**
*   Append results into CSV files under `bm1_results/`

### 3. Optional: Single Configuration Run

```bash
docker compose run --rm host ./run_bm1.sh iris mlp
```

***

## End-to-End Benchmark Steps

1.  **Host key generation**
    *   Ephemeral RSA keypair generated on host

2.  **Host-side training**
    *   LR or MLP trained on cleartext data
    *   Train/Test accuracy printed

3.  **Encryption**
    *   Test dataset encrypted using AES-256-CBC
    *   AES key wrapped using RSA public key

4.  **TDX execution**
    *   Encrypted payload uploaded to real TDX VM
    *   `td-run` executed inside TD
    *   Per-sample latency measured using `std::chrono`

5.  **Result retrieval**
    *   Encrypted results returned to host
    *   Host decrypts and extracts summary metrics

6.  **CSV aggregation**
    ```csv
    dataset,model,iteration,avg_latency_us,train_acc_percent,test_acc_percent,tdx_acc_percent
    ```

***

## Dataset Configuration

| Dataset | Train Size | Test Size | Notes               |
| ------- | ---------: | --------: | ------------------- |
| Iris    |      \~120 |      \~30 | 80/20 split         |
| WDBC    |      \~450 |     \~110 | Normalized          |
| MNIST   |       5000 |        48 | Scaled pixel values |

***

## Model Hyperparameters

| Dataset | Model | Hidden Units |            Epochs |   LR |
| ------- | ----- | -----------: | ----------------: | ---: |
| Iris    | MLP   |           10 |               200 | 0.10 |
| WDBC    | MLP   |           16 |               200 | 0.05 |
| MNIST   | MLP   |           64 |                50 | 0.15 |
| All     | LR    |            – | dataset dependent | same |

***

## Experimental Results (Summary)

Mean latency across 10 runs:

| Dataset | Model | Avg Latency (µs) | TDX Acc (%) |
| ------- | ----- | ---------------: | ----------: |
| Iris    | LR    |           \~0.34 |         100 |
| Iris    | MLP   |           \~2.46 |         100 |
| WDBC    | LR    |           \~0.67 |         100 |
| WDBC    | MLP   |           \~2.64 |         100 |
| MNIST   | LR    |          \~16.25 |        89.6 |
| MNIST   | MLP   |          \~75.47 |       93.75 |

TDX accuracy always matches host-side test accuracy.

***

## Security Notes & Limitations

*   RSA keypair is **generated on the host** and private key is uploaded to the TD
*   SSH uses password-based authentication (`sshpass`)
*   No remote attestation is performed
*   This is a **functional research benchmark**, not a production security system

***

## Purpose of BM1

BM1 establishes a **TDX-only confidential inference baseline**.

It answers:

*   What is the cost of inference inside a real TDX VM?
*   How does latency scale with feature dimension?
*   How much overhead does MLP add compared to LR?

BM1 intentionally avoids:

*   FHE overhead
*   Ciphertext management complexity
*   Attestation cost

Those are addressed in later benchmarks.

***

## Citation

If referenced in an artifact evaluation:

> **BM1** is a TDX-only benchmark that measures per-sample inference latency and accuracy for LR and MLP models executed inside a real Intel TDX guest VM using encrypted input/output transport.

```

