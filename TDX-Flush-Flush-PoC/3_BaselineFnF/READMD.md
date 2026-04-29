# 3_BaselineFnF – Native Flush+Flush Timing Baseline

This directory contains a **native Flush+Flush baseline microbenchmark** used to measure the timing gap between two cache states on the same machine:

- **Hit / Cached** state  
- **Miss / Uncached** state  

The goal of this benchmark is to provide a **reference timing distribution** for the `clflush` instruction before moving to the TDX experiments. In other words, this code is **not a TDX guest-host attack**. It is a **local native baseline** that helps validate whether Flush+Flush can separate cached and uncached lines on the target CPU.

---

## Purpose

This program was designed to answer a simple question:

> When `clflush` is executed on a cache line that is already cached versus one that is already evicted, how different is the measured cycle count?

The output can be used to:

- build a histogram of Flush+Flush timings
- estimate a hit/miss threshold
- observe platform-specific timing behavior
- justify the thresholding logic later used in the TDX leakage experiments

---

## Directory Layout

```text
3_BaselineFnF/
├── native_baseline_ff.cpp      # source code for the native timing baseline
├── native_baseline_ff          # compiled binary
└── native_baseline_ff.csv      # raw timing output
```

---

## What the Code Does

The program performs two separate timing phases on the same target cache line.

### Phase 1: Miss / Uncached measurement
The target line is first flushed so that it is not resident in cache.  
Then the program measures the cycle cost of calling `clflush` again on that already-evicted line.

### Phase 2: Hit / Cached measurement
The target line is first flushed, then accessed once to bring it back into cache.  
After that, the program measures the cycle cost of `clflush` on the cached line.

For each trial, the measured cycle counts are stored and written to a CSV file.

---

## Measurement Design

The benchmark includes several design choices to reduce noise and make the baseline reproducible.

### Core pinning
The process is pinned to a specific CPU core using `sched_setaffinity()`.

### Cycle timing
The code measures `clflush` latency with:

- `rdtscp`
- `mfence`

This makes the timing sequence more stable and reduces reordering effects.

### Warm-up
The program touches the buffer before measurement so that paging and first-touch effects are reduced.

### Large sample count
By default, the program collects **500,000 samples** for each class:

- `hit`
- `miss`

This makes it easier to generate a stable histogram.

---

## Build Requirements

You need:

- Linux on x86_64
- a CPU supporting `clflush` and `rdtscp`
- `g++` with C++17 support

No external crypto library is needed for this baseline.

---

## Build Instructions

This directory does not use a Makefile. Build it directly with `g++`.

From inside `3_BaselineFnF/`:

```bash
g++ -O2 -std=c++17 native_baseline_ff.cpp -o native_baseline_ff
```

### Recommended clean rebuild

If you want to rebuild from scratch:

```bash
rm -f native_baseline_ff
g++ -O2 -std=c++17 native_baseline_ff.cpp -o native_baseline_ff
```

### Check that the binary was created

```bash
ls -l native_baseline_ff
```

---

## Run Instructions

Default run:

```bash
./native_baseline_ff
```

This uses:

- `core = 2`
- `samples = 500000`

### Run on a different core

```bash
./native_baseline_ff --core 3
```

### Run with a different number of samples

```bash
./native_baseline_ff --samples 100000
```

### Run with both options

```bash
./native_baseline_ff --core 3 --samples 200000
```

---

## Output

The program writes the raw cycle measurements to:

```text
native_baseline_ff.csv
```

The CSV format is:

```csv
hit,miss
523,612
511,605
...
```

Each row contains one pair of measurements:

- `hit` = `clflush` cycles when the line was cached
- `miss` = `clflush` cycles when the line was uncached

---

## Example Console Output

```text
[native_baseline] Pinned to core 2, Samples: 500000
[native_baseline] Saved results to native_baseline_ff.csv
```

---

## How to Interpret the Results

This benchmark is meant to show that Flush+Flush timing depends on cache state.

After collecting the CSV, you can plot:

- two histograms
- kernel density estimates
- mean / median / percentile summaries

Depending on the processor generation and platform behavior, the separation may appear in either direction.  
On some systems, cached lines may produce higher `clflush` latency than uncached lines.  
On others, the ordering may differ. That is why this baseline is useful before selecting a threshold for later experiments.

---

## Relation to the TDX Experiments

This directory is only the **baseline timing study**.

It does **not**:

- launch a TDX guest
- probe IVSHMEM
- communicate with a guest victim
- recover AES T-table accesses
- inspect CKKS refresh behavior

Those behaviors belong to the later TDX experiments under `2_TDX_Leak/`.

This baseline is only used to establish that **Flush+Flush timing can distinguish cache states on the test machine**.

---

## Notes

- Run on an otherwise quiet machine if possible.
- Avoid heavy background processes during measurement.
- Keep the pinned core consistent across repeated runs.
- If you compare multiple machines, collect a separate baseline on each machine.

---

## Suggested Next Step

After generating `native_baseline_ff.csv`, use a plotting script or notebook to visualize:

- hit distribution
- miss distribution
- overlap between the two classes
- a possible classification threshold

This output can then support the rationale for your TDX Flush+Flush threshold selection.

