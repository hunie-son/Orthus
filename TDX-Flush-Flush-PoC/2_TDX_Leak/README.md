# 2_TDX_Leak
**TDX-based Flush+Flush evaluation with a host-side probe and a guest-side victim inside a TDVM**

This directory contains the **TDX version** of the Flush+Flush proof of concept used in the paper workflow.

The most important role distinction is the following.

- **Guest** runs **inside the TDX TDVM** and executes the victim workload.
- **Host** runs **outside the TDVM** and performs **only the Flush+Flush probe**.

In other words, the host is **not** the victim and does **not** run the protected workload. It only measures cache-flush timing while coordinating with the guest through shared state.

---

## What this directory is for

`2_TDX_Leak` is the TDX experiment directory that evaluates whether a host can observe useful Flush+Flush leakage while the victim runs inside a trusted domain VM.

The same host-side probing engine is reused across different guest workloads. In this repository, the host side stores separate output folders for:

- **AES_results** for AES T-table experiments
- **CKKS_results** for CKKS-related experiments

This matches the paper's evaluation logic, where Flush+Flush is used both for AES T-table leakage experiments and for the CKKS refresh-node assessment. The paper reports that AES shows a strong structured leakage pattern, while CKKS refresh activity produces a much more uniform pattern. fileciteturn19file0L1161-L1276

---

## Role separation

### Guest side

The **Guest** component is the victim side and is intended to run **inside the TDX guest VM**.

Depending on the experiment, the guest may:

- access AES T-table related data
- execute a shared-memory control version for validation
- execute a CKKS-related routine used in the Orthus evaluation flow

The paper's TDX attack model distinguishes between a cooperative baseline using shared state and the private-memory TDX attack used for AES leakage evaluation. fileciteturn19file0L1103-L1159 fileciteturn20file0L1161-L1218

### Host side

The **Host** component is the attacker-side measurement process and runs **outside the TDVM**.

Its job is to:

- map the shared region such as `/dev/shm/ivshmem_ff`
- calibrate Flush+Flush timing
- synchronize with the guest through a compact control structure
- probe cache-line candidates and record timing-derived hit counts
- write CSV-style results for later plotting

The current host source tree shows that the probing binary is `host_spy_ff`, built from `Host/src/host_spy_ff.cpp`, and that the Host directory stores separate `AES_results/` and `CKKS_results/` folders. fileciteturn23file0L1-L16 fileciteturn23file0L17-L124

---

## Repository layout

```text
2_TDX_Leak/
├── Guest/
│   └── guest-side victim programs that run inside the TDX TDVM
└── Host/
    ├── AES_results/
    ├── CKKS_results/
    ├── scripts/
    │   ├── plot_spy.py
    │   └── generated heatmaps
    └── src/
        ├── host_spy_ff.cpp
        ├── host_spy_ff
        └── Makefile
```

The top-level PoC tree confirms this split into `Guest/` and `Host/`, and the Host subtree contains the source, plotting script, and result folders. fileciteturn23file0L1-L16

---

## Host implementation summary

The host probe implementation is in:

```text
2_TDX_Leak/Host/src/host_spy_ff.cpp
```

This program:

1. pins itself to a selected CPU core
2. opens and maps `/dev/shm/ivshmem_ff`
3. calibrates a Flush+Flush threshold
4. performs a handshake with the guest using the shared control structure
5. scans a probe region line by line
6. records hit counts for each plaintext group and probe offset
7. prints CSV-formatted results to stdout

The current source uses a shared-memory control block with fields such as `pt`, `token`, `ack`, `iters`, and `finished`, then outputs rows beginning with `probe_offset`. fileciteturn23file0L17-L124

Although some variable names still reflect the AES experiment, the host logic is fundamentally the **generic probe side**. The experiment meaning changes based on what the **guest inside the TDVM** is doing, not based on a different host probing primitive.

---

## Build

### Host build

From the `Host/src` directory:

```bash
make
```

This builds:

```text
host_spy_ff
```

The provided Makefile confirms that `host_spy_ff.cpp` is compiled into `host_spy_ff`. fileciteturn23file0L125-L132

### Guest build

Build commands depend on which guest victim you keep in `Guest/`. The guest binary must be compiled **inside the TDX guest VM** or prepared for execution there.

---

## How to run the experiment

### Step 1
Start the guest victim **inside the TDX TDVM**.

### Step 2
Run the host probe **outside the TDVM** on the host side.

Example host command from `Host/src`:

```bash
./host_spy_ff --core 3 --iters 5000000 > ../AES_results/result_aes_ttable_final.csv
```

For CKKS-style experiments, use the same host probe binary and redirect output into the CKKS results folder instead.

Example:

```bash
./host_spy_ff --core 3 --iters 5000000 > ../CKKS_results/result_ckks_result.csv
```

The host code currently supports at least `--core` and `--iters`. fileciteturn23file0L17-L124

---

## Plotting results

The plotting script is located at:

```text
2_TDX_Leak/Host/scripts/plot_spy.py
```

At the moment, the script uses a hard-coded CSV filename, so you should update `CSV_FILE` before plotting a different result file.

Run it from the `Host/scripts` directory:

```bash
python3 plot_spy.py
```

The current script is configured for AES-style output and saves `spy_heatmap_aes_ttable.png`. fileciteturn23file0L133-L156

---

## Expected output interpretation

### AES experiment

For AES T-table experiments, the host output is expected to show a structured leakage pattern. In the paper, this appears as a clear diagonal in the T-table heatmap for both native execution and the TDX private-memory attack. fileciteturn19file0L1219-L1276

### CKKS experiment

For CKKS-related experiments, the host still uses the same probing primitive, but the observed pattern is expected to be much more uniform. The paper explains that the CKKS decryption routine in the Orthus refresh node does not expose the same kind of secret-dependent memory access pattern as AES T-table lookups. fileciteturn19file0L1219-L1276

---

## What to say clearly in the artifact

When describing this PoC, use wording like the following.

> In `2_TDX_Leak`, the **guest** is the victim process running **inside the TDX TDVM**, while the **host** remains **outside the TDVM** and performs only the Flush+Flush probing and result collection.

That sentence is the key distinction this directory should make explicit.

---

## Notes

- The current host source still uses some AES-oriented names such as `shared_te0`, but the underlying probe logic is reusable.
- The meaning of a run is determined by the guest workload and the output folder, such as `AES_results/` or `CKKS_results/`.
- If you want a cleaner artifact, the next cleanup step would be to rename the host binary and variables from AES-specific names to more neutral probe-side names.

