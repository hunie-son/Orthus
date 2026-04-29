# TDX-Flush-Flush-PoC

This repository contains a set of proof-of-concept experiments for studying the
**Flush+Flush cache side channel** in three progressively more realistic settings:

1. **Native AES leakage baseline**
2. **TDX guest leakage experiments**
3. **Native Flush+Flush timing baseline**

The purpose of this top-level README is to explain how the three folders fit
together as one PoC suite. Each subdirectory should still have its own local
README with detailed build and run instructions.

---

## Repository Layout

```text
TDX-Flush-Flush-PoC/
├── 1_Native_AES_Leak/
├── 2_TDX_Leak/
└── 3_BaselineFnF/
```

---

## High-Level Goal

The overall goal of this repository is to demonstrate and compare:

- the basic timing separation of `clflush` on cached vs uncached lines
- native AES T-table leakage using Flush+Flush
- Flush+Flush-style probing against a workload running inside a **TDX guest VM**
- the difference between an AES-style secret-dependent access pattern and a more
  uniform CKKS-style access pattern

In short, the repository moves from a simple native timing baseline to a native
AES leakage experiment and then to a TDX-based host-versus-guest setup.

---

## Folder-by-Folder Description

## 1_Native_AES_Leak

This directory contains the **native AES T-table leakage baseline**.

### What it is
- A single-machine Flush+Flush PoC targeting the AES T-table region in OpenSSL
- No TDX guest is involved
- No host/guest split is involved
- Used to confirm that the expected AES leakage pattern appears clearly in a
  native setting

### Why it exists
This experiment acts as the most direct reference point for the later TDX
experiments. If the native attack shows a clear diagonal leakage pattern, that
provides a baseline for comparing what happens when similar probing is attempted
in the TDX setting.

### Typical contents
- OpenSSL-based AES target code
- plotting scripts
- raw CSV or text results
- heatmaps showing leakage intensity by probe offset and plaintext group

---

## 2_TDX_Leak

This directory contains the **TDX-based leakage experiments**.

### What it is
This folder is organized as a **split experiment**:

- **Guest/**
  - code that runs **inside the TDX guest VM**
  - acts as the victim workload
- **Host/**
  - code that runs **outside the TDX guest**
  - performs the **Flush+Flush probing only**

This role split is important:

> The **Guest** performs the victim-side accesses inside the TDVM.  
> The **Host** does not execute the protected workload.  
> The **Host only probes and records timing behavior**.

### Why it exists
This directory is the main TDX PoC. It is used to study how the host can observe
or fail to observe patterns while a victim runs inside a TDX-protected VM.

### Host side
The host side includes:
- a generic Flush+Flush probing binary
- result folders for AES and CKKS experiments
- plotting scripts for heatmaps

Even if some filenames mention AES, the host logic is conceptually the same:
it pins to a core, probes a target region, records `clflush` timings, and writes
results. The actual meaning of the signal depends on what the **Guest** is doing.

### Guest side
The guest side includes the victim workloads that run inside the TDX VM.

Depending on the experiment, the guest may:
- generate AES-style target accesses
- expose a signal through coordinated shared-memory behavior
- execute CKKS-related routines with more uniform memory access behavior

### Important interpretation
For this folder, always read the experiment as:

- **Guest = victim running inside TDVM**
- **Host = external probe process only**

This is the main architectural distinction between `2_TDX_Leak/` and the native
directories.

### Typical result categories
- **AES_results/**
  - expected to show stronger, more structured leakage
- **CKKS_results/**
  - expected to look more diffuse or uniform compared to AES
- scripts for generating heatmaps from raw CSV/text output

---

## 3_BaselineFnF

This directory contains the **native Flush+Flush timing baseline**.

### What it is
- A local microbenchmark
- Measures the raw timing difference of `clflush` on:
  - cached lines
  - uncached lines
- No AES leakage recovery
- No TDX guest
- No host/guest communication

### Why it exists
This directory is used to establish the platform-specific timing behavior of
Flush+Flush before interpreting later attack results.

This is useful because different machines can show different timing orderings or
different amounts of overlap between cached and uncached flushes.

### Output
Typically this directory produces:
- a CSV file containing raw `hit` and `miss` timings
- data suitable for histograms or threshold selection

---

## Recommended Reading Order

If you are new to this PoC suite, read and reproduce the folders in this order.

### Step 1
Start with `3_BaselineFnF/`

This tells you whether your machine shows a usable timing gap between cached and
uncached `clflush` measurements.

### Step 2
Move to `1_Native_AES_Leak/`

This shows the classic native AES T-table leakage pattern and gives you a visual
reference for what a successful structured signal looks like.

### Step 3
Then use `2_TDX_Leak/`

This is the actual TDX-oriented PoC where:
- the **Guest** runs inside the TDVM
- the **Host** acts only as the probe process

This step allows you to compare structured AES-style behavior against the more
uniform CKKS-style behavior under the TDX experiment setup.

---

## Conceptual Experiment Flow

```text
3_BaselineFnF
    ↓
Validate raw Flush+Flush timing separation

1_Native_AES_Leak
    ↓
Confirm native AES T-table leakage pattern

2_TDX_Leak
    ↓
Run guest victim inside TDVM
Run host probe outside TDVM
Compare AES-oriented and CKKS-oriented patterns
```

---

## What This Repository Does Not Assume

This repository does not assume that all three folders use the same threat model
or the same execution boundary.

- `1_Native_AES_Leak` is a native local baseline
- `3_BaselineFnF` is a native timing microbenchmark
- `2_TDX_Leak` is the only folder that explicitly uses the **Host vs TDVM Guest**
  split

Because of that, the role of the binaries must always be interpreted relative to
their folder.

---

## Practical Notes

- Build and run instructions should be taken from each folder's local README.
- Result filenames may reflect different tuning runs or intermediate experiments.
- Plotting scripts are best treated as result-processing utilities, not core
  attack logic.
- The most important experimental distinction in this repository is whether the
  code runs:
  - natively on the same machine, or
  - as **Host probe vs Guest victim inside a TDX VM**

---

## Summary

This repository is organized around one main message:

- `3_BaselineFnF/` establishes the raw Flush+Flush timing behavior
- `1_Native_AES_Leak/` demonstrates native AES leakage
- `2_TDX_Leak/` evaluates the same general probing idea in a TDX setting where  
  the **Guest runs the victim inside the TDVM** and the **Host performs probing only**

For detailed reproduction, consult the README inside each subdirectory.

