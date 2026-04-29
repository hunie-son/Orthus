# BM3 - TDX Assisted FHE Unified Benchmark

**Hybrid confidential inference using OpenFHE on the host with ciphertext refresh inside a real Intel TDX VM**

This repository implements **Benchmark 3 (BM3)**, the hybrid design in the artifact evaluation pipeline.

BM3 combines:

- **host-side CKKS inference with OpenFHE**
- **remote ciphertext refresh inside a real TDX guest**
- **OpenMP-based parallel execution on the host**
- **dataset/model sweeps across multiple thread counts**

Unlike BM1, which measures TDX-only plaintext inference, and BM2, which performs pure OpenFHE bootstrapped inference on the host, BM3 offloads ciphertext refresh to a **real TDX VM** using a decrypt -> re-encode -> re-encrypt workflow. This makes BM3 the hybrid **TEE + FHE** benchmark in the PoC benchmark suite.

---

## Benchmark Scope

BM3 evaluates the following configurations.

### Datasets
- **Iris**
- **WDBC**
- **MNIST**

### Models
- **Logistic Regression (LR)**
- **One-hidden-layer MLP**

### Thread configurations
- **1, 2, 4, 6, 8, 10, 12, 14, 16 OpenMP threads**

### Repetitions
- **5 runs** per dataset/model/thread configuration

### Output metric
- **Average latency per sample**
- Unit: **milliseconds per sample**

### Total configurations
- 3 datasets
- 2 models
- 9 thread counts
- 5 repetitions

Total runs in the full sweep: **270**

---

## What BM3 Measures

BM3 is designed to measure the cost of **hybrid confidential inference** where:

1. the host performs most CKKS inference logic,
2. ciphertext refresh is delegated to a real TDX guest over SSH/SCP,
3. final predictions are decrypted locally for accuracy checking.

### Included
- Cleartext model training on the host
- OpenFHE key generation and ciphertext packing
- Host-side encrypted inference
- Remote TDX-assisted ciphertext refresh
- OpenMP scaling experiments
- End-to-end batch processing over encrypted test sets
- Accuracy comparison against host-side test accuracy

### Not included
- Remote attestation
- Production-grade key provisioning
- Mutual authentication beyond password-based SSH
- A fully containerized TD service in the active runtime path
- Native OpenFHE EvalBootstrap inside the host inference loop

Instead of OpenFHE bootstrapping, BM3 refreshes ciphertexts by sending them to the TDX guest, where they are decrypted and freshly re-encrypted using the same CKKS context and keys.

---

## High-Level Architecture

```text
Docker Host Container
├── host1
│   ├── load data
│   ├── train LR / MLP in clear
│   ├── generate CKKS context and keys
│   ├── pack encrypted test samples
│   └── write /data artifacts
│
├── host2
│   ├── load encrypted inputs and weights
│   ├── run encrypted inference with OpenMP
│   ├── call td_refresh() when ciphertext refresh is needed
│   └── write encrypted logits to /data/C2.bin
│
└── host1-decrypt
    ├── decrypt final logits
    ├── recover predictions
    └── compute final accuracy

Remote TDX VM
├── receives refresh request ciphertext
├── decrypts using CKKS secret key
├── re-encodes plaintext at a fresh level
├── re-encrypts with CKKS public key
└── returns refreshed ciphertext
```

---

## Active Execution Flow

BM3 uses only the **host** service in `docker-compose.yml`. The TDX guest is **external** and is reached over SSH.

### Phase 1. Host1 setup
`host1-run` performs:

- dataset loading and preprocessing
- LR or MLP training in clear
- train/test accuracy reporting
- CKKS context generation
- key generation
- rotation key generation
- ciphertext packing and encryption of test inputs
- serialization of keys and metadata into `/data`

### Phase 2. Host2 encrypted inference
`host2-run` performs:

- encrypted LR or MLP forward pass
- OpenMP parallel execution
- serialized network refresh through `td_refresh()`
- output ciphertext generation into `/data/C2.bin`

### Phase 3. Host1 decryption
`host1-decrypt` performs:

- ciphertext decryption
- packed slot decoding
- argmax prediction recovery
- final accuracy computation

---

## Remote TDX Refresh Mechanism

The core BM3 difference is the **TDX-assisted refresh path**.

Inside `host2-run.cpp`, `td_refresh()`:

1. serializes a ciphertext to `/data/req_<pid>_<ctr>.bin`
2. uploads it to the TDX guest using `scp`
3. invokes `/data/td-bootstrap` on the guest using `ssh`
4. downloads the refreshed ciphertext
5. replaces the old ciphertext locally

Inside the TDX guest, `td-bootstrap.cpp`:

1. loads `cc.json`, `pk.bin`, and `sk.bin`
2. deserializes the incoming ciphertext
3. decrypts it
4. extracts packed values
5. creates a fresh CKKS plaintext
6. re-encrypts with the public key
7. writes the refreshed ciphertext back

This is not a cryptographic bootstrap in the OpenFHE sense. It is a **TEE-assisted ciphertext refresh** implemented through decrypt and re-encrypt inside the protected guest.

---

## Repository Structure

```text
TDX_FHE_Unified_TDXVM_OpenMP_MT_final_v2/
├── auto_bm.sh                     # Full BM3 benchmark driver
├── docker-compose.yml             # Host container only
├── docker_cleanup.sh
├── bm3_results_update/            # Aggregated benchmark CSV files
│   ├── iris_lr.csv
│   ├── iris_mlp.csv
│   ├── wdbc_lr.csv
│   ├── wdbc_mlp.csv
│   ├── mnist_lr.csv
│   └── mnist_mlp.csv
├── data/                          # Shared runtime artifacts and datasets
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
│   ├── run_fhe.sh                 # Orchestrates host1 -> host2 -> host1-decrypt
│   ├── run_fhe_original.sh
│   ├── host1/
│   │   ├── main.cpp               # training + CKKS setup + encrypted packing
│   │   ├── host1-decrypt.cpp      # final decryption and accuracy recovery
│   │   ├── model.cpp/.hpp
│   │   ├── data.cpp/.hpp
│   │   └── Makefile
│   └── host2/
│       ├── host2-run.cpp          # encrypted inference + td_refresh calls
│       ├── utils.hpp              # sigmoid polynomial and helpers
│       └── Makefile
└── td/
    ├── Dockerfile                 # reference TD build path
    ├── Makefile                   # legacy / inactive for active compose path
    └── src/
        ├── td-bootstrap.cpp       # TDX-side refresh binary
        ├── td-bootstrap-init
        └── utils.hpp
```

### Important note about `td/`
The active runtime does **not** launch a `td` service through Docker Compose. Instead:

- `host/Dockerfile` compiles `td/src/td-bootstrap.cpp`
- `run_fhe.sh` uploads the resulting `td-bootstrap` binary and OpenFHE shared libraries to the remote TDX VM
- the remote TDX VM executes refresh requests on demand

So the `td/` directory acts mainly as a **source/reference location** for the TDX-side refresh logic.

---

## Build and Run

### 1. Build the host image
```bash
docker compose build
```

### 2. Make scripts executable
```bash
chmod +x auto_bm.sh docker_cleanup.sh host/run_fhe.sh
```

### 3. Run the full BM3 suite
```bash
./auto_bm.sh
```

Results are written to:

```bash
bm3_results_update/
```

### 4. Run a single configuration manually
```bash
docker compose run \
  -e OMP_NUM_THREADS=16 \
  -e BATCH_CAP=100 \
  --rm host ./run_fhe.sh mnist mlp
```

---

## BM3 Runtime Details

### Docker environment
The host container builds:

- OpenFHE from source
- `host1-run`
- `host1-decrypt`
- `host2-run`
- `td-bootstrap`

### SSH environment
`run_fhe.sh` expects access to a real TDX VM through:

- `TD_HOST`
- `TD_PORT`
- `TD_USER`
- `TD_PASS`

The default values are set in `docker-compose.yml`:

```yaml
environment:
  - TD_HOST=host.docker.internal
  - TD_PORT=10022
  - TD_USER=root
  - TD_PASS=123456
```

### Shared data path
The shared Docker volume `/data` stores:

- packed ciphertext inputs
- result ciphertexts
- serialized keys
- model weights
- packing metadata
- prediction outputs

---

## CKKS and Packing Configuration

From `host1/main.cpp`, BM3 uses the following CKKS settings:

- Security level: **HEStd_128_classic**
- Ring dimension: **65536**
- Scaling modulus size: **59**
- Multiplicative depth: **10**
- Scaling technique: **FLEXIBLEAUTO**

Packing parameters are derived from input dimension:

- `STRIDE = 2 * nextPow2(input_dim_with_bias)`
- default `BATCH_CAP = 100`, overridden by environment variable if provided
- `pack_meta.txt` stores:
  - `stride`
  - `batchcap`
  - `nsamples`

These metadata are used by both `host2-run` and `host1-decrypt` so that partial final batches are handled correctly.

---

## Model and Dataset Configuration

### Datasets
- **Iris**: randomized 80/20 split from `iris_new.csv`
- **WDBC**: randomized 80/20 split from `wdbc.csv`
- **MNIST**: 5000 training samples and 48 test samples

### Model hyperparameters

| Dataset | Model | Hidden Units | Epochs | Learning Rate |
|---|---:|---:|---:|---:|
| Iris | MLP | 10 | 200 | 0.10 |
| WDBC | MLP | 16 | 200 | 0.10 |
| MNIST | MLP | 64 | 50 | 0.15 |
| All datasets | LR | not used | same dataset schedule | same dataset schedule |

### Weight scaling
- LR weights are scaled by **0.5**
- MLP weights are scaled by **1.0**

---

## Parallelism Design

BM3 uses OpenMP in `host2-run.cpp`, but refresh traffic is **network serialized**.

This is important for interpretation.

### Parallel sections
- hidden-layer computations for MLP
- class-wise logit computations for LR

### Serialized section
All calls to `td_refresh()` are protected by:

- `std::mutex g_net_mtx`
- unique request/response filenames
- a single SSH/SCP path per refresh call

This means BM3 is **not fully parallel end to end**. Local encrypted computation can scale with threads, but remote refresh remains a bottleneck.

That is why some configurations show:

- strong gains from 1 -> 2 or 1 -> 4 threads
- weaker gains after that
- non-monotonic behavior at higher thread counts

---

## Result File Format

Each aggregated CSV file uses the following header:

```csv
dataset,model,threads,iteration,avg_latency_ms,train_acc_percent,test_acc_percent,fhe_acc_percent
```

Each file contains all 5 runs for all 9 thread counts of one dataset/model pair.

Examples:

- `bm3_results_update/iris_lr.csv`
- `bm3_results_update/mnist_mlp.csv`

---

## Experimental Results Summary

The table below reports:

- host-side train accuracy
- host-side test accuracy
- final BM3 encrypted accuracy
- best observed average latency among the tested thread counts
- the thread count where that best mean occurred

| Dataset | Model | Train Acc (%) | Test Acc (%) | BM3 Acc (%) | Best Mean Latency (ms/sample) | Best Threads | 1-thread Mean (ms/sample) |
|---|---:|---:|---:|---:|---:|---:|---:|
| Iris | LR | 97.50 | 100.00 | 100.00 | 238.71 | 6 | 313.78 |
| Iris | MLP | 97.50 | 100.00 | 80.00 | 1209.01 | 10 | 1856.75 |
| WDBC | LR | 100.00 | 100.00 | 100.00 | 61.12 | 8 | 61.60 |
| WDBC | MLP | 100.00 | 100.00 | 100.00 | 995.55 | 14 | 1718.00 |
| MNIST | LR | 92.66 | 89.58 | 89.58 | 1507.05 | 8 | 2868.76 |
| MNIST | MLP | 90.38 | 93.75 | 93.75 | 14114.28 | 4 | 28423.31 |

### Observations
- **BM3 is much faster than BM2** for every configuration shown here, which is consistent with replacing host-side bootstrapping with TDX-assisted refresh.
- **LR remains highly accurate across all three datasets**, and encrypted accuracy matches host test accuracy.
- **WDBC MLP also preserves full accuracy** under the BM3 pipeline.
- **MNIST MLP retains full host test accuracy** while showing about a 2x reduction from 1 thread to the best measured configuration.
- **Iris MLP is the notable outlier**, where encrypted accuracy drops to 80% despite 100% host test accuracy. This suggests a model-specific approximation or refresh sensitivity issue rather than a general BM3 failure.

---

## Security Boundaries and Limitations

BM3 is a research benchmark, not a production deployment.

### What BM3 demonstrates
- encrypted host-side inference
- confidential refresh inside a real TDX guest
- clear separation between host computation and protected refresh
- practical hybrid TEE + FHE execution

### Current limitations
- SSH uses password authentication via `sshpass`
- no remote attestation
- keys are uploaded to the TDX guest as serialized files
- refresh uses decrypt and re-encrypt, not cryptographic bootstrap
- network traffic is serialized through one refresh path
- compose setup does not instantiate the TDX guest directly

These limitations are acceptable for artifact evaluation and benchmarking, but they should not be interpreted as a production security architecture.

---

## BM1 vs BM2 vs BM3

| Benchmark | Core idea | Protected component | Refresh strategy | Main metric |
|---|---|---|---|---|
| BM1 | TDX-only baseline | plaintext inference inside TDX | none | guest inference latency |
| BM2 | FHE-only baseline | host-side CKKS inference | OpenFHE EvalBootstrap | encrypted latency |
| BM3 | hybrid TEE + FHE | host-side CKKS + TDX refresh | decrypt and re-encrypt inside TDX | encrypted latency with remote refresh |

BM3 is the bridge benchmark showing how a TEE can reduce the cost of ciphertext refresh while keeping most of the encrypted inference pipeline on the host side.

---

## Key Source Files

### Benchmark driver
- `auto_bm.sh`

### Orchestration
- `host/run_fhe.sh`

### Host setup and encryption
- `host/host1/main.cpp`

### Final decryption and prediction recovery
- `host/host1/host1-decrypt.cpp`

### Hybrid inference with remote refresh
- `host/host2/host2-run.cpp`

### Polynomial activation approximation
- `host/host2/utils.hpp`

### TDX-side refresh binary
- `td/src/td-bootstrap.cpp`

---

## Suggested Citation Description

If you need a one-paragraph description for an artifact evaluation or appendix, you can use this.

> **BM3** is a hybrid TEE + FHE benchmark that performs CKKS-based encrypted inference on the host while delegating ciphertext refresh to a real Intel TDX guest VM. The host trains LR and MLP models, packs encrypted test inputs, executes encrypted forward passes with OpenMP parallelism, and invokes a remote TDX-side decrypt -> re-encode -> re-encrypt service whenever ciphertext refresh is required. Final logits are decrypted locally for accuracy measurement, and results are aggregated across datasets, models, and thread counts.


