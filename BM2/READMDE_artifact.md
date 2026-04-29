# BM2 – FHE-Only Unified Benchmark  
**Bootstrapped CKKS Inference with OpenFHE and OpenMP**

This repository implements **Benchmark 2 (BM2)**, the **FHE-only baseline** in the artifact evaluation benchmark suite.

BM2 measures the cost of **fully homomorphic encrypted inference** using **OpenFHE CKKS bootstrapping**, without relying on Intel TDX or any trusted execution environment. All encrypted computation is performed on the host, and OpenMP is used to study **thread-level scaling**.

BM2 serves as the **fully encrypted baseline** before introducing the hybrid **TEE + FHE** design in BM3.

---

## Benchmark Scope

BM2 evaluates the following configurations:

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

That gives **270 benchmark runs** in the full BM2 sweep.

---

## What BM2 Measures (and What It Does Not)

### ✅ Included
- Model training in cleartext on the host
- CKKS key generation, rotation key generation, and bootstrapping key generation
- Packed ciphertext construction for encrypted test inputs
- Encrypted LR or MLP inference using OpenFHE
- Host-side bootstrapping with `EvalBootstrap`
- OpenMP-based thread scaling
- Final decryption and encrypted-accuracy measurement

### ❌ Not Included
- Intel TDX or SGX
- Trusted execution environments
- Remote ciphertext refresh
- Remote attestation
- Production-grade key lifecycle management
- Host↔guest network timing

BM2 intentionally isolates **pure FHE inference cost**.

---

## System Architecture

```text
Host (Docker Container)
 ├─ Host1
 │   ├─ Load and normalize data
 │   ├─ Train LR / MLP in clear
 │   ├─ Generate CKKS context and keys
 │   ├─ Pack and encrypt test samples
 │   └─ Serialize ciphertext batches
 │
 ├─ Host2
 │   ├─ Load encrypted inputs and model weights
 │   ├─ Run encrypted inference with OpenFHE
 │   ├─ Apply CKKS bootstrapping as needed
 │   ├─ Use OpenMP for parallel execution
 │   └─ Write encrypted class scores
 │
 └─ Host1-decrypt
     ├─ Decrypt encrypted outputs
     ├─ Recover predictions
     ├─ Compare with true labels
     └─ Write CSV output
```

---

## Repository Structure

```text
FHE_only_Unified_OpenMP_MT_final/
├── auto_bm2.sh                 # Full benchmark driver
├── docker-compose.yml          # Host container definition
├── docker_cleanup.sh
├── bm2_results/
├── bm2_results_updated/        # Aggregated CSV results used by current script
│   ├── iris_lr.csv
│   ├── iris_mlp.csv
│   ├── wdbc_lr.csv
│   ├── wdbc_mlp.csv
│   ├── mnist_lr.csv
│   └── mnist_mlp.csv
├── data/
│   ├── iris_new.csv
│   ├── wdbc.csv
│   ├── mnist_train.csv
│   ├── mnist_test.csv
│   ├── pima.csv
│   ├── get_mnist.py
│   └── get_wdbc.py
├── host/
│   ├── Dockerfile
│   ├── run_fhe.sh              # Host1 → Host2 → Host1-decrypt orchestration
│   ├── host1/
│   │   ├── main.cpp            # training + keygen + packing + encryption
│   │   ├── host1-decrypt.cpp   # final decryption and accuracy recovery
│   │   ├── model.*             # LR + MLP implementation
│   │   ├── data.*              # dataset loading & normalization
│   │   ├── Makefile
│   │   └── sigmoid_coeffs.txt
│   └── host2/
│       ├── host2-run.cpp       # encrypted inference + bootstrapping
│       ├── utils.hpp           # polynomial sigmoid helpers
│       ├── Makefile
│       └── chebyshev.py
└── openfhe-install/
```

---

## Execution Workflow

### 1. Build the Host Container

```bash
docker compose build
```

### 2. Run the Full Benchmark Suite

```bash
chmod +x auto_bm2.sh host/run_fhe.sh docker_cleanup.sh
./auto_bm2.sh
```

This will:

- iterate over all dataset/model combinations
- sweep all OpenMP thread counts
- execute each configuration **5 times**
- append results into CSV files under `bm2_results_updated/`

### 3. Optional: Single Configuration Run

```bash
docker compose run -e OMP_NUM_THREADS=8 -e BATCH_CAP=100 --rm host ./run_fhe.sh mnist mlp
```

---

## End-to-End Benchmark Steps

1. **Host-side training**
   - LR or MLP is trained on cleartext data
   - Train/Test accuracy is reported

2. **CKKS setup**
   - Host1 generates the CKKS context
   - public, secret, multiplication, rotation, and bootstrapping keys are created

3. **Packing and encryption**
   - Test samples are packed into ciphertext batches
   - packed ciphertexts are serialized to `/data/C1.bin`

4. **Encrypted inference**
   - Host2 loads encrypted inputs and model weights
   - performs encrypted LR or MLP forward computation
   - applies `EvalBootstrap` during the encrypted path
   - uses OpenMP to parallelize classes or hidden units

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

Additional BM2 runtime settings:

- **Ring dimension**: 65536
- **Scaling mod size**: 59
- **First mod size**: 60
- **Level budget**: `{8, 8}`
- **Packing cap**: default `BATCH_CAP=100`

---

## Experimental Results (Summary)

Best observed mean latency across tested thread settings:

| Dataset | Model | Best Threads | Best Avg Latency (ms) | FHE Acc (%) |
| ------- | ----- | -----------: | --------------------: | ----------: |
| Iris    | LR    |            4 |             ~2323.37 |      100.00 |
| Iris    | MLP   |           14 |             ~2523.77 |       96.67 |
| WDBC    | LR    |            1 |             ~1227.88 |      100.00 |
| WDBC    | MLP   |           16 |             ~1456.18 |      100.00 |
| MNIST   | LR    |           10 |             ~4843.77 |       89.58 |
| MNIST   | MLP   |           16 |            ~22609.30 |       83.33 |

Representative scaling observations:

- BM2 shows **large latency reductions** for MLP workloads as thread count increases.
- LR workloads are generally more stable and preserve accuracy more easily.
- Encrypted accuracy matches cleartext test accuracy for **all LR runs** and **WDBC MLP**.
- Accuracy drops appear in **Iris MLP** and **MNIST MLP**, reflecting approximation error in the encrypted nonlinear path.

---

## Security Notes & Limitations

- No TDX or trusted hardware is used in BM2
- All protection comes from encrypted computation in OpenFHE
- Key generation is part of the benchmark path and is regenerated each run
- MLP inference uses a **polynomial approximation** of sigmoid rather than exact nonlinear evaluation
- This is a **functional research benchmark**, not a production FHE inference service

---

## Purpose of BM2

BM2 establishes the **FHE-only confidential inference baseline**.

It answers:

- What is the cost of fully encrypted inference without TDX?
- How much benefit does OpenMP provide for bootstrapped CKKS inference?
- Which workloads preserve accuracy well under approximate encrypted computation?
- How much latency gap remains before a hybrid TEE + FHE design becomes attractive?

BM2 intentionally avoids:

- TDX-based refresh
- remote trusted execution
- attestation and secure provisioning complexity

Those are introduced in BM3.

---

## Citation

If referenced in an artifact evaluation:

> **BM2** is an FHE-only benchmark that measures per-sample encrypted inference latency and accuracy for LR and MLP models using OpenFHE CKKS bootstrapping with OpenMP-based thread scaling.

