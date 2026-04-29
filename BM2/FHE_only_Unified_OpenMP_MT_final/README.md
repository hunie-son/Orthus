# BM2 | FHE-Only Unified OpenFHE Benchmark

This repository implements **Benchmark 2 (BM2)** for the artifact evaluation proof of concept. BM2 is the **pure FHE baseline** in the benchmark suite. Unlike BM1, which executes inference inside a real Intel TDX guest, BM2 removes TDX entirely and measures the cost of **fully homomorphic inference with OpenFHE CKKS bootstrapping**, plus OpenMP-based thread scaling.

BM2 currently supports:

- **Datasets**: Iris, WDBC, MNIST
- **Models**: Logistic Regression (LR), Multi-Layer Perceptron (MLP)
- **Thread settings**: 1, 2, 4, 6, 8, 10, 12, 14, 16
- **Runs per configuration**: 5
- **Metric of interest**: average encrypted inference latency per sample, reported in **milliseconds**
- **Result folder used by the current benchmark script**: `bm2_results_updated/`

In total, BM2 evaluates:

- 3 datasets
- 2 models
- 9 thread counts
- 5 repetitions per thread setting

That gives **270 benchmark runs** in the full BM2 sweep.

---

## What BM2 Measures

BM2 is designed to isolate the cost and behavior of **FHE-only encrypted inference** using OpenFHE. The workflow is split into two host-side stages inside one Dockerized environment:

1. **Host1**
   - loads and normalizes the dataset
   - trains a cleartext LR or MLP model
   - reports cleartext train and test accuracy
   - builds the CKKS context
   - generates keys, rotation keys, and bootstrapping keys
   - packs and encrypts the test set into ciphertext batches

2. **Host2**
   - loads the OpenFHE context and evaluation keys
   - loads the trained model weights
   - performs encrypted inference using bootstrapped CKKS operations
   - uses OpenMP to parallelize work across hidden units or classes
   - serializes encrypted class-score outputs

3. **Host1 decrypt**
   - decrypts the encrypted outputs
   - reconstructs per-sample class scores
   - performs argmax classification
   - reports final FHE accuracy

This makes BM2 the natural baseline for comparison against any later **TEE + FHE hybrid** design, because BM2 keeps the computation fully on the FHE path without relying on TDX or external refresh.

---

## How BM2 Differs from BM1

BM1 and BM2 intentionally measure different protection models.

| Benchmark | Isolation mechanism | Encryption during inference | Main latency unit | Main purpose |
|---|---|---|---|---|
| BM1 | Intel TDX guest execution | No FHE. Standard encrypted transport plus plaintext inference inside TD | microseconds | TDX-only baseline |
| BM2 | OpenFHE CKKS bootstrapped inference | Yes. Model inputs remain encrypted during computation | milliseconds | FHE-only baseline |

So BM2 should be expected to be much slower than BM1, but it offers a much stronger encrypted-computation baseline.

---

## Quick Start

### 1. Move into the benchmark directory

```bash
cd ~/Research/Benchmark/PoC/BM2/FHE_only_Unified_OpenMP_MT_final
```

### 2. Build the Docker image

```bash
docker compose build
```

### 3. Run the full BM2 benchmark suite

```bash
chmod +x auto_bm2.sh host/run_fhe.sh docker_cleanup.sh
./auto_bm2.sh
```

This will:

- build the image if needed
- iterate over all dataset and model pairs
- sweep the OpenMP thread counts
- run each configuration 5 times
- append results to `bm2_results_updated/{dataset}_{model}.csv`
- shut down Docker resources at the end

### 4. Optional single-run command

```bash
docker compose run -e OMP_NUM_THREADS=8 -e BATCH_CAP=100 --rm host ./run_fhe.sh mnist mlp
```

You can replace `mnist mlp` with any supported pair:

- `iris lr`
- `iris mlp`
- `wdbc lr`
- `wdbc mlp`
- `mnist lr`
- `mnist mlp`

You can also change `OMP_NUM_THREADS` and `BATCH_CAP` from the command line.

---

## Prerequisites

Before running BM2, make sure the following are available.

### Host-side requirements

- Docker and Docker Compose
- Enough CPU cores and memory for large OpenFHE key generation and bootstrapping
- Internet access during image build, since the Dockerfile clones the OpenFHE source tree from GitHub
- Dataset files under `data/`

### Runtime assumptions

BM2 assumes:

- a single Docker service named `host`
- a shared volume mounted at `/data`
- plaintext dataset files mounted read-only at `/app/data`

No TDX VM, SSH, or remote guest is required in BM2.

---

## Repository Structure

```text
FHE_only_Unified_OpenMP_MT_final/
├── auto_bm2.sh
├── bm2_results/
├── bm2_results_updated/
│   ├── iris_lr.csv
│   ├── iris_mlp.csv
│   ├── mnist_lr.csv
│   ├── mnist_mlp.csv
│   ├── wdbc_lr.csv
│   └── wdbc_mlp.csv
├── data/
│   ├── get_mnist.py
│   ├── get_wdbc.py
│   ├── iris.csv
│   ├── iris_new.csv
│   ├── mnist_test.csv
│   ├── mnist_train.csv
│   ├── pima.csv
│   └── wdbc.csv
├── docker-compose.yml
├── docker_cleanup.sh
├── host/
│   ├── Dockerfile
│   ├── run_fhe.sh
│   ├── host1/
│   │   ├── data.cpp
│   │   ├── data.hpp
│   │   ├── fit_sigmoid.py
│   │   ├── host1-decrypt.cpp
│   │   ├── main.cpp
│   │   ├── Makefile
│   │   ├── model.cpp
│   │   ├── model.hpp
│   │   └── sigmoid_coeffs.txt
│   └── host2/
│       ├── chebyshev.py
│       ├── host2-run.cpp
│       ├── Makefile
│       └── utils.hpp
├── docker-compose.yml
└── openfhe-install/
```

---

## Which Files Matter Most

### Top level

- **`auto_bm2.sh`**  
  Full benchmark driver. Sweeps dataset, model, thread count, and repetition index.

- **`docker-compose.yml`**  
  Defines the `host` service and mounts the shared `/data` volume plus plaintext datasets.

- **`docker_cleanup.sh`**  
  Utility script for pruning Docker cache, images, and volumes after heavy benchmark runs.

### Docker build

- **`host/Dockerfile`**  
  Builds the benchmark image, clones and compiles OpenFHE, then compiles the Host1 and Host2 binaries.

### Runtime orchestration

- **`host/run_fhe.sh`**  
  Main BM2 execution script. It cleans stale outputs, runs Host1 for training and encryption, runs Host2 for encrypted inference, then runs Host1 decryption and prints final accuracy.

### Host1

- **`host/host1/main.cpp`**  
  Trains the LR or MLP model, configures CKKS, generates keys, computes packing metadata, and encrypts the packed test set.

- **`host/host1/data.cpp` / `data.hpp`**  
  Loads datasets, performs fixed train/test splitting, applies normalization, and writes test labels for later decryption-based evaluation.

- **`host/host1/model.cpp` / `model.hpp`**  
  Cleartext training and accuracy reporting for LR and MLP.

- **`host/host1/host1-decrypt.cpp`**  
  Loads the secret key, decrypts encrypted class-score outputs, reconstructs predictions, and computes final FHE accuracy.

### Host2

- **`host/host2/host2-run.cpp`**  
  Performs the FHE inference path. This includes masked dot products, CKKS bootstrapping, MLP polynomial activation, OpenMP parallelization, per-sample timing logs, and serialization of class-score ciphertexts.

- **`host/host2/utils.hpp`**  
  Contains the polynomial sigmoid approximation used for encrypted MLP activation evaluation.

---

## End-to-End BM2 Workflow

### Step 1. Cleartext training and dataset preparation

`host1/main.cpp` loads one of the supported datasets and uses a fixed random seed for reproducible splitting. It then trains either:

- **LR**
- **MLP**

The code prints cleartext train and test accuracy before any encrypted execution begins.

### Step 2. CKKS context and key generation

BM2 uses OpenFHE CKKS with the following key settings:

- ring dimension: `65536`
- scaling modulus size: `59`
- first modulus size: `60`
- bootstrapping level budget: `{8, 8}`
- multiplicative depth: `approxBootstrapDepth + 10`

The benchmark enables:

- `PKE`
- `KEYSWITCH`
- `LEVELEDSHE`
- `ADVANCEDSHE`
- `FHE`

It then generates:

- public key
- secret key
- evaluation multiplication keys
- automorphism keys
- bootstrapping keys

These are serialized under `/data/keys/` and `/data/sk.bin`.

### Step 3. Packing strategy

Host1 packs multiple test samples into one CKKS ciphertext.

Important details:

- `slots = ringDim / 2 = 32768`
- `input_dim_pack = D + 1`, where the extra slot is the bias
- `STRIDE = 2 * nextPow2(D + 1)`
- `BATCH_CAP` defaults to `100` but can be overridden through an environment variable
- `pack_meta.txt` stores:
  - `stride`
  - `batchcap`
  - `nsamples`

This metadata lets Host2 and the decryptor correctly interpret both full and partial batches.

### Step 4. Encrypted inference in Host2

Host2 loads the encrypted test batches from `/data/C1.bin` and the model weights from `/data/W.csv` or `/data/W1.csv` plus `/data/W2.csv`.

#### LR path

For each class:

- multiply packed ciphertext by packed class weights
- sum the first `D + 1` slots inside each sample block
- bootstrap the result
- keep only the block-start slot for each sample

#### MLP path

For each hidden unit:

- compute encrypted dot product
- bootstrap
- apply polynomial sigmoid approximation
- keep only the block-start slot

Then for each output class:

- linearly combine the encrypted hidden activations
- serialize one ciphertext per class

### Step 5. Final decryption and accuracy computation

`host1-decrypt.cpp` decrypts the serialized class-score ciphertexts from `/data/C2.bin`, reconstructs each sample's scores from block-start slots, applies argmax, and computes final FHE accuracy against `/data/test_labels_unified.txt`.

---

## Notes on the Encrypted MLP Path

The MLP inference path is approximate for two reasons:

1. **CKKS is approximate arithmetic**
2. **sigmoid is replaced by a low-degree polynomial approximation**

In `host/host2/utils.hpp`, the active setting is a degree-3 polynomial:

```cpp
return {0.5, 0.15012, 0.0, -0.00159305};
```

Because of that, BM2 should not always be expected to exactly match cleartext MLP accuracy, especially on harder datasets.

---

## Benchmark Automation

The full sweep is controlled by `auto_bm2.sh`.

### Active benchmark settings

```bash
THREADS_LIST=(1 2 4 6 8 10 12 14 16)
RUNS_PER_CONFIG=5
DATASETS=("iris" "wdbc" "mnist")
MODELS=("lr" "mlp")
OUTPUT_DIR="bm2_results_updated"
```

### Output format

Each result file uses this schema:

```text
dataset,model,threads,iteration,avg_latency_ms,train_acc_percent,test_acc_percent,fhe_acc_percent
```

So each row captures:

- dataset
- model
- OpenMP thread count
- repetition number
- average encrypted inference latency per sample
- cleartext train accuracy
- cleartext test accuracy
- FHE accuracy after decrypting the result ciphertexts

---

## Current BM2 Results Summary

The current result files under `bm2_results_updated/` show the following best observed thread setting for each dataset and model pair.

| Dataset | Model | Best threads | Best avg latency (ms/sample) | 1-thread avg (ms/sample) | Speedup vs 1 thread | Train acc (%) | Test acc (%) | FHE acc (%) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| IRIS | LR | 4 | 2323.3733 | 6739.0467 | 2.90x | 97.5 | 100 | 100 |
| IRIS | MLP | 14 | 2523.7733 | 23289.5333 | 9.23x | 97.5 | 100 | 96.6667 |
| WDBC | LR | 1 | 1227.8789 | 1227.8789 | 1.00x | 100 | 100 | 100 |
| WDBC | MLP | 16 | 1456.1825 | 19979.7298 | 13.72x | 100 | 100 | 100 |
| MNIST | LR | 10 | 4843.7667 | 45574.9375 | 9.41x | 92.66 | 89.5833 | 89.5833 |
| MNIST | MLP | 16 | 22609.3042 | 301775.8958 | 13.35x | 90.38 | 93.75 | 83.3333 |

### Observations from the current runs

1. **LR preserves accuracy extremely well**
   - Iris LR, WDBC LR, and MNIST LR match cleartext test accuracy exactly in the current results.
   - This is expected because LR avoids encrypted nonlinear activation approximation during inference.

2. **MLP benefits more from threading**
   - WDBC MLP improves from about 19979.73 ms/sample at 1 thread to about 1456.18 ms/sample at 16 threads.
   - MNIST MLP improves from about 301775.90 ms/sample at 1 thread to about 22609.30 ms/sample at 16 threads.

3. **Some workloads plateau early**
   - Iris LR improves strongly from 1 to 4 threads, then largely plateaus.
   - WDBC LR changes very little across thread counts, which suggests limited parallel benefit for that workload at the current packing and class size.

4. **MLP accuracy is not always preserved**
   - Iris MLP drops from 100% cleartext test accuracy to 96.6667% FHE accuracy.
   - MNIST MLP drops from 93.75% cleartext test accuracy to 83.3333% FHE accuracy.
   - This likely reflects approximate encrypted nonlinear evaluation, especially the polynomial sigmoid approximation, rather than a simple training issue.

---

## Reading the Result Files

Examples:

- `bm2_results_updated/iris_lr.csv`
- `bm2_results_updated/iris_mlp.csv`
- `bm2_results_updated/wdbc_lr.csv`
- `bm2_results_updated/wdbc_mlp.csv`
- `bm2_results_updated/mnist_lr.csv`
- `bm2_results_updated/mnist_mlp.csv`

A few useful checks when reviewing outputs:

- compare `test_acc_percent` vs `fhe_acc_percent`
- compare the 1-thread run against the best thread count
- look for plateau regions where more threads stop helping
- check whether larger models benefit more from parallelism than smaller ones

---

## Known Limitations

BM2 is a benchmark prototype, not a production FHE service. A few practical limitations are worth stating clearly.

1. **Build time is heavy**  
   The Docker image builds OpenFHE from source, so initial setup is expensive.

2. **Key generation is part of the benchmark run path**  
   Host1 regenerates keys and bootstrapping material for each run. This is useful for an end-to-end benchmark, but it means BM2 is not just measuring steady-state inference.

3. **Approximate MLP activation**  
   The encrypted MLP path uses a polynomial sigmoid approximation rather than the true sigmoid.

4. **Thread scaling is workload-dependent**  
   Not every dataset and model pair benefits equally from additional OpenMP threads.

5. **The current benchmark uses a single containerized host service**  
   BM2 is intentionally simpler than later hybrid designs and does not model distributed trusted execution.

---

## Suggested Use in the Benchmark Suite

BM2 is the right baseline when you want to answer questions like:

- How expensive is pure FHE inference compared with BM1 TDX-only execution?
- Which workloads benefit from more CPU threads under OpenFHE bootstrapping?
- How much accuracy is preserved when moving from cleartext inference to approximate encrypted inference?
- What latency gap remains before a hybrid TEE + FHE design becomes attractive?

In that sense, BM2 serves as the **fully encrypted baseline** that later PoC benchmarks should outperform or at least meaningfully contextualize.

---

## Reproducibility Notes

To reproduce the current BM2 result layout:

```bash
docker compose build
./auto_bm2.sh
```

To reproduce a specific row pattern, run a single configuration repeatedly, for example:

```bash
for i in 1 2 3 4 5; do
  docker compose run -e OMP_NUM_THREADS=16 -e BATCH_CAP=100 --rm host ./run_fhe.sh wdbc mlp
done
```

If Docker storage becomes bloated after repeated runs:

```bash
./docker_cleanup.sh
```

---

