# BM1 - TDX-Only Unified Benchmark for Artifact Evaluation

This repository implements **Benchmark 1 (BM1)** for the artifact evaluation proof of concept. The goal of BM1 is to measure the cost of running standard machine learning inference inside a **real Intel TDX guest VM**, without FHE, while still using encrypted transport for the test payload between the host container and the TD guest.

BM1 currently supports:

- **Datasets**: Iris, WDBC, MNIST
- **Models**: Logistic Regression (LR), Multi-Layer Perceptron (MLP)
- **Metric of interest**: Average per-sample inference latency inside the TDX guest, reported in **microseconds**
- **Repetitions**: 10 runs per dataset/model pair

In total, BM1 evaluates **6 configurations**:

1. iris + lr
2. iris + mlp
3. wdbc + lr
4. wdbc + mlp
5. mnist + lr
6. mnist + mlp

---

## What BM1 Measures

BM1 is designed to isolate the overhead and behavior of **TDX-only protected inference**. The host trains a model in the clear, prepares the encrypted test payload, uploads the required files to a real TDX VM over SSH, triggers inference inside the guest, then downloads the encrypted results for decryption and summary reporting.

Unlike the later hybrid TEE + FHE benchmarks, BM1 does **not** include homomorphic encryption, ciphertext refresh, or packed encrypted inference. It focuses on a simpler setting:

- model training on the host
- encrypted payload transport to the TD guest
- inference in the TD guest
- encrypted result return to the host

This makes BM1 a clean baseline for comparing against more advanced confidential computing pipelines.

---

## Current Execution Model

The active BM1 workflow in this repository uses:

- a **Dockerized host container**
- a **remote real TDX VM** reached over SSH
- a benchmark runner that uploads the `td-run` binary and benchmark files into the guest VM

Important implementation detail:

- The current PoC **generates an ephemeral RSA keypair on the host side**, then copies `td_priv.pem` into the TDX guest for decryption.
- This is acceptable for a **functional benchmark prototype**, but it is **not** a production-grade attestation or key provisioning design.
- In other words, BM1 demonstrates **TDX-isolated execution** and encrypted transport behavior, but it does **not** claim strong end-to-end cryptographic key isolation from the host.

That distinction is important for artifact evaluation and should be stated clearly.

---

## Quick Start

### 1. Move into the benchmark directory

```bash
cd ~/Research/Benchmark/PoC/BM1/TDX_only_Unified_TDXVM
```

### 2. Build the host image

```bash
docker compose build
```

### 3. Run the full BM1 suite

```bash
chmod +x auto_bm1.sh host/run_bm1.sh docker_cleanup.sh
./auto_bm1.sh
```

This will:

- build the Docker image if needed
- iterate over all dataset/model combinations
- run each configuration 10 times
- append results to `bm1_results/{dataset}_{model}.csv`
- shut down Docker resources at the end

### 4. Optional single-run command

If you only want one benchmark run instead of the full suite:

```bash
docker compose run --rm host ./run_bm1.sh iris mlp
```

Replace `iris mlp` with any supported pair:

- `iris lr`
- `iris mlp`
- `wdbc lr`
- `wdbc mlp`
- `mnist lr`
- `mnist mlp`

---

## Prerequisites

Before running BM1, make sure the following are available.

### Host-side requirements

- Docker and Docker Compose
- Network connectivity from the container to the TDX VM
- The benchmark datasets under `host/data/`

### TDX VM requirements

The current scripts assume the TDX VM is reachable with these settings:

- host: `host.docker.internal`
- port: `10022`
- user: `root`

These values are set through:

- `docker-compose.yml`
- `host/run_bm1.sh`

The TDX guest should also:

- allow SSH access
- have a writable `/data` directory
- be able to execute the uploaded `/data/td-run`
- expose TDX guest status in `dmesg`

### Security note

The current script uses password-based SSH login configured in `host/run_bm1.sh`. That is fine for a controlled PoC environment, but it should be replaced before any wider release or external sharing.

---

## Repository Structure

```text
TDX_only_Unified_TDXVM/
├── auto_bm1.sh
├── bm1_results/
│   ├── iris_lr.csv
│   ├── iris_mlp.csv
│   ├── mnist_lr.csv
│   ├── mnist_mlp.csv
│   ├── wdbc_lr.csv
│   └── wdbc_mlp.csv
├── docker-compose.yml
├── docker_cleanup.sh
├── entrypoint.sh
├── host/
│   ├── Dockerfile
│   ├── data/
│   │   ├── iris.csv
│   │   ├── iris_new.csv
│   │   ├── mnist_test.csv
│   │   ├── mnist_train.csv
│   │   ├── pima.csv
│   │   └── wdbc.csv
│   ├── entrypoint.sh
│   ├── run_bm1.sh
│   ├── src/
│   │   ├── crypto.cpp
│   │   ├── crypto.hpp
│   │   ├── data.cpp
│   │   ├── data.hpp
│   │   ├── host.cpp
│   │   ├── model.cpp
│   │   ├── model.hpp
│   │   ├── td-run.cpp
│   │   └── utils.hpp
│   └── td_bootstrap.sh
├── Readme.md
├── secrets/
└── td/
    ├── Dockerfile
    └── src/
        ├── td.cpp
        └── td-init.cpp
```

---

## Which Files Are Actually Used in BM1

The most relevant files for the active BM1 execution path are:

### Top-level

- **`auto_bm1.sh`**  
  Runs the full benchmark suite across all dataset/model pairs and stores aggregated CSVs.

- **`docker-compose.yml`**  
  Defines the `host` service used for the benchmark.

- **`docker_cleanup.sh`**  
  Cleans Docker cache, images, volumes, and system artifacts.

### Host side

- **`host/Dockerfile`**  
  Builds the benchmark container, compiles `host` and `td-run`, and packages the runtime.

- **`host/run_bm1.sh`**  
  Core orchestration script. It performs local cleanup, key generation, model training, payload upload, remote TD execution, result download, and final decryption.

- **`host/src/host.cpp`**  
  Main host-side benchmark program. It supports two modes:
  - `encrypt`: load data, train model, export weights, encrypt test payload
  - `decrypt`: decrypt TD result file and print summary metrics

- **`host/src/data.cpp` / `data.hpp`**  
  Dataset loading, train/test splitting, and normalization.

- **`host/src/model.cpp` / `model.hpp`**  
  LR and MLP training and evaluation logic.

- **`host/src/crypto.cpp` / `crypto.hpp`**  
  AES and RSA helper functions used by the host.

- **`host/src/td-run.cpp`**  
  The binary uploaded to the TDX guest. It decrypts the test payload, loads model weights, runs guest-side inference, times per-sample latency, and returns encrypted results.

- **`host/src/utils.hpp`**  
  Shared helper code used by the TD-side executable.

### Less central or currently inactive pieces

- **`td/`** contains a TD-oriented Docker build and initialization code, but the current BM1 flow does **not** launch a `td` service through `docker-compose.yml`.
- **`entrypoint.sh`** files exist, but the main benchmark path is driven by `auto_bm1.sh` and `host/run_bm1.sh`.

This is worth documenting because the repository contains both active benchmark code and earlier or alternative paths.

---

## End-to-End Workflow

BM1 proceeds in the following order.

### Step 1. Build the host benchmark container

`auto_bm1.sh` starts by calling:

```bash
docker compose build
```

The host Docker image compiles:

- `host` from `host.cpp`, `crypto.cpp`, `data.cpp`, `model.cpp`
- `td-run` from `td-run.cpp`

### Step 2. Select dataset/model pair

`auto_bm1.sh` loops over:

- datasets: `iris`, `wdbc`, `mnist`
- models: `lr`, `mlp`
- iterations: `1..10`

Each pair writes to one aggregated CSV:

```text
bm1_results/{dataset}_{model}.csv
```

### Step 3. Generate benchmark artifacts on the host

Inside `host/run_bm1.sh`:

1. Temporary benchmark files in `/data` are removed.
2. A fresh RSA keypair is generated:
   - `/data/td_priv.pem`
   - `/data/td_pub.pem`
3. The host program is called in `encrypt` mode:

```bash
./host "$DATASET" "$MODEL" "encrypt"
```

In this phase the host:

- loads and preprocesses the dataset
- trains LR or MLP in cleartext
- reports training and test accuracy
- writes model weights to `/data/W1.csv` and optionally `/data/W2.csv`
- writes `/data/config.txt` to identify LR vs MLP
- encrypts the test split into `/data/C2.bin`
- wraps the AES session key into `/data/C1.bin`

### Step 4. Upload files to the real TDX VM

`host/run_bm1.sh` then uploads the following into the guest `/data` directory:

- `td-run`
- `C1.bin`
- `C2.bin`
- `config.txt`
- `td_priv.pem`
- `W1.csv`
- `W2.csv` if needed

### Step 5. Execute inference inside the TDX guest

The benchmark checks for evidence of TDX guest state via:

```bash
dmesg | grep -i tdx | head -n 3
```

Then it runs:

```bash
chmod +x /data/td-run && /data/td-run
```

Inside `td-run.cpp`, the guest:

1. reads `C1.bin` and `C2.bin`
2. unwraps the AES key with `td_priv.pem`
3. decrypts the test payload
4. loads model weights from `W1.csv` and possibly `W2.csv`
5. performs LR or MLP forward inference
6. measures **per-sample latency in microseconds**
7. computes accuracy
8. writes encrypted output to `/data/Cres.bin`

The output format includes per-sample rows and a summary section:

```text
sample_id,predicted,actual,latency_us
...
SUMMARY:
Accuracy: ...
Avg Latency: ... us/sample
```

### Step 6. Download and decrypt results

The host downloads `Cres.bin` and runs:

```bash
./host "$DATASET" "$MODEL" "decrypt"
```

This decrypts the result blob and extracts the two lines used by the benchmark suite:

- `Accuracy: ...`
- `Avg Latency: ...`

### Step 7. Append aggregated CSV row

`auto_bm1.sh` parses the console output and appends one row to the corresponding result CSV:

```text
dataset,model,iteration,avg_latency_us,train_acc_percent,test_acc_percent,tdx_acc_percent
```

---

## Dataset Handling

### Iris

- Loaded from `iris_new.csv`
- Random 80/20 split
- Standardized using train-set mean and standard deviation

### WDBC

- Loaded from `wdbc.csv`
- Falls back to `pima.csv` if `wdbc.csv` is unavailable
- Random 80/20 split
- Standardized using train-set mean and standard deviation

### MNIST

- Loaded from `mnist_train.csv` and `mnist_test.csv`
- Uses up to **5000 training samples** and **48 test samples**
- Pixel values are scaled as:

```text
((pixel / 255.0) - 0.5) * 0.125
```

---

## Model Configuration

The host program adapts hyperparameters by dataset.

### Logistic Regression

Training uses softmax regression with L2-style decay.

### MLP

A one-hidden-layer MLP is used.

Current settings from `host.cpp`:

| Dataset | Hidden size H | Epochs | Learning rate |
|---|---:|---:|---:|
| iris | 10 | 200 | 0.10 |
| wdbc | 16 | 200 | 0.05 |
| mnist | 64 | 50 | 0.15 |

For LR, the same epoch and learning-rate schedule is reused per dataset, without a hidden layer.

---

## Output Files

### Aggregated benchmark results

Each configuration produces one CSV under `bm1_results/`:

- `iris_lr.csv`
- `iris_mlp.csv`
- `wdbc_lr.csv`
- `wdbc_mlp.csv`
- `mnist_lr.csv`
- `mnist_mlp.csv`

Each row corresponds to one run:

```csv
dataset,model,iteration,avg_latency_us,train_acc_percent,test_acc_percent,tdx_acc_percent
iris,lr,1,0.3243,97.5,100,100
```

### Temporary runtime artifacts

The host and TD exchange these files via `/data`:

- `td_priv.pem`
- `td_pub.pem`
- `C1.bin`
- `C2.bin`
- `Cres.bin`
- `config.txt`
- `W1.csv`
- `W2.csv`

---

## Example Console Behavior

A typical BM1 run prints messages like:

```text
>>> RUNNING REAL TDX BENCHMARK (Password Auth)
>>> Dataset: iris | Model: mlp
>>> Target: root@host.docker.internal

[System] Generating RSA Keypair...
[Host] Phase 1: Training & Encryption...
[Network] Uploading payload to TDX VM...
[Check] Verifying TDX Guest Status:
[Network] Executing Inference on Real Hardware...
[TD] sample #0 time=2.281 us
...
[Host] Phase 3: Decryption...
>>> BENCHMARK 1 FINAL RESULTS <<<
Accuracy: 100%
Avg Latency: 2.2739 us/sample
```

---

## BM1 Experimental Results

The following summary is based on the result CSVs currently stored under `bm1_results/`.

### Raw result files used

- `iris_lr.csv`
- `iris_mlp.csv`
- `wdbc_lr.csv`
- `wdbc_mlp.csv`
- `mnist_lr.csv`
- `mnist_mlp.csv`

### Summary across 10 runs per configuration

| Dataset | Model | Mean latency (us) | Min (us) | Max (us) | Train acc (%) | Test acc (%) | TDX acc (%) |
|---|---|---:|---:|---:|---:|---:|---:|
| iris | lr  | 0.3437 | 0.3223 | 0.3847 | 97.5 | 100.0 | 100.0 |
| iris | mlp | 2.4612 | 2.0860 | 3.5089 | 97.5 | 100.0 | 100.0 |
| wdbc | lr  | 0.6722 | 0.3646 | 2.4396 | 100.0 | 100.0 | 100.0 |
| wdbc | mlp | 2.6401 | 0.9308 | 3.8140 | 100.0 | 100.0 | 100.0 |
| mnist | lr  | 16.2490 | 13.8102 | 21.0637 | 92.66 | 89.5833 | 89.5833 |
| mnist | mlp | 75.4654 | 71.2654 | 81.2700 | 90.38 | 93.75 | 93.75 |

### Key observations

1. **TDX accuracy matches host-side test accuracy in all reported runs.**  
   This shows that the guest-side inference path is functionally consistent with host-side evaluation.

2. **MLP is consistently slower than LR**, which is expected because it adds at least one hidden-layer forward pass and more matrix-vector work.

3. **MNIST is much slower than Iris and WDBC**, which is also expected due to its much larger feature dimension of 784.

4. **WDBC LR shows a few latency outliers**, especially one run at `2.4396 us`, while most runs are close to `0.36 - 0.44 us`. This suggests some runtime noise or measurement variability rather than a change in correctness.

5. **MNIST MLP achieves the highest test and TDX accuracy among the MNIST configurations**, but with significantly higher latency than LR.

These results make BM1 a useful baseline for later comparisons against more expensive protected inference designs.

---

## Interpretation of the Benchmark

BM1 should be interpreted as a **plain-model confidential execution baseline**.

It answers questions such as:

- What is the guest-side latency for standard LR and MLP inference in a real TDX VM?
- How does latency scale from low-dimensional inputs like Iris to higher-dimensional inputs like MNIST?
- How much extra cost appears when moving from LR to MLP in the same TDX-protected setting?

It does **not** answer:

- the cost of FHE bootstrapping
- packed encrypted inference cost
- TEE + FHE hybrid refresh cost
- remote attestation overhead
- production-grade confidential key management cost

Those belong to later benchmarks.

---

## Limitations

The current BM1 implementation has several important limitations.

1. **Ephemeral keypair generated on host**  
   The host generates the RSA keypair and uploads the private key to the TD guest. This is convenient for prototyping but weaker than true enclave-generated or attested keying.

2. **Password-based SSH for orchestration**  
   The benchmark uses password-authenticated SSH via `sshpass`. That is practical for automation, but not ideal from a security engineering perspective.

3. **TD VM is external to Compose**  
   The benchmark depends on an externally reachable real TDX VM. Docker Compose only manages the host container in the current active path.

4. **Small MNIST test set**  
   The current configuration uses only 48 MNIST test samples, so the reported latency is useful for controlled benchmarking, but the accuracy is not meant to be a large-scale ML result.

5. **Repository includes inactive or legacy components**  
   Some files such as `td/` and the entrypoint scripts are present but are not central to the current `auto_bm1.sh` execution path.

---

## Reproducibility Notes

To reproduce the current BM1 results as closely as possible:

- use the same datasets under `host/data/`
- keep the same random seed behavior in the code
- keep the same TDX VM environment and SSH path
- run the full `./auto_bm1.sh` suite
- use the generated CSVs in `bm1_results/` as the ground-truth benchmark outputs

The benchmark already includes a resume check:

```bash
if grep -q "^$dataset,$model,$i," "$CSV_FILE"; then
    echo "    [Skip] Run: $i (Found in CSV)"
    continue
fi
```

So interrupted runs can continue without losing completed iterations.

---

## Recommended Future Improvements

Several improvements would make BM1 stronger as a research artifact.

1. Move private-key generation fully inside the TD guest.
2. Add remote attestation and measured launch validation.
3. Replace password SSH with key-based authentication.
4. Log full host-to-TD round-trip time in addition to guest-only inference latency.
5. Add automated summary generation for mean, standard deviation, and confidence intervals.
6. Clean up unused repository paths or label them more clearly as legacy/alternate implementations.

---

## Suggested Citation Description for the Artifact

If this benchmark is referenced in an artifact appendix or evaluation section, a concise description could be:

> **BM1** is a TDX-only baseline benchmark that trains LR and MLP models on the host, encrypts the test payload, executes inference inside a real TDX guest VM, and reports guest-side per-sample latency and accuracy across Iris, WDBC, and MNIST.

---

## Contact Between Components at a Glance

For quick reference, the active path is:

```text
auto_bm1.sh
  -> docker compose run --rm host ./run_bm1.sh <dataset> <model>
      -> ./host <dataset> <model> encrypt
      -> scp C1.bin/C2.bin/W*.csv/config/td_priv.pem + td-run to TDX VM
      -> ssh into TDX VM and execute /data/td-run
      -> scp /data/Cres.bin back
      -> ./host <dataset> <model> decrypt
      -> append parsed metrics to bm1_results/<dataset>_<model>.csv
```

---

## Final Takeaway

BM1 provides the first benchmark point in the artifact evaluation pipeline. It is intentionally simpler than later benchmarks and serves as the baseline for understanding the cost of confidential inference in a real TDX environment before introducing more advanced protected computation techniques.

