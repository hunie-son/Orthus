# Native AES Flush+Flush T-table Spy

This directory contains a native Flush+Flush proof of concept that targets the AES T-table implementation inside OpenSSL `libcrypto`.

The experiment runs on a single machine without virtualization and measures cache-line activity during AES-style table lookups. The current canonical source file is `src/aes_ff_spy_single_v2.c`.

The output is written as a CSV-like text file and can be visualized as heatmaps showing which AES T-table cache lines are touched for each plaintext first-byte group.

---

## Directory overview

Current layout:

```text
1_Native_aes_leak/
├── README.md
├── openssl-1.1.1w/
├── openssl-1.1.1w.tar.gz
├── results/
│   ├── result_3_17_10000iter.txt
│   └── result_3_17_10000iter_v2.txt
├── scripts/
│   ├── plot_spy.py
│   ├── spy_heatmap.png
│   └── spy_heatmap_1000.png
└── src/
    ├── aes_ff_spy_single_v2.c
    ├── ff_utils.h
    ├── Makefile
    └── aes_ff_spy_single_v2
```

### Important note about paths

The current source code hardcodes:

```c
#define LIBCRYPTO_PATH "./openssl/libcrypto.so"
```

and the current `src/Makefile` expects a local OpenSSL install under `src/openssl/`.

That means the binary should be built and executed from inside `src/`, with OpenSSL installed locally at:

```text
src/openssl/libcrypto.so
```

---

## High level design

1. The spy maps the OpenSSL `libcrypto.so` shared object as read-only.
2. The AES `Te0` file offset is set manually using a fixed offset.
3. A probe region around `Te0` is divided into 64-byte cache lines.
4. For each probe line and each plaintext-byte group:
   - the line is flushed
   - the victim performs one synthetic T-table lookup
   - Flush+Flush latency is measured again
5. Latency below a calibrated threshold is classified as a cache hit.
6. Hit counts are accumulated into a matrix.
7. The matrix is saved as text/CSV-style output and then converted into a heatmap with Python.

---

## Prerequisites

- x86_64 Linux
- CPU supporting `clflush` and `rdtscp`
- `gcc` with C11 support
- `make`
- Python 3 with `numpy` and `matplotlib`

---

## OpenSSL setup

This PoC uses OpenSSL 1.1.1w so that AES T-tables remain present.

From the project root:

```bash
cd ~/Research/Benchmark/PoC/TDX-Flush-Flush-PoC/1_Native_aes_leak
```

If needed, unpack the tarball:

```bash
tar xf openssl-1.1.1w.tar.gz
```

Then build and install a local copy into `src/openssl` so it matches the current source and Makefile paths:

```bash
cd openssl-1.1.1w
./config --prefix=$(pwd)/../src/openssl
make -j
make install_sw
cd ..
```

After installation, the shared object used by the spy should be:

```text
src/openssl/libcrypto.so
```

---

## Build

Build from inside `src/`:

```bash
cd src
make
```

This produces:

```text
aes_ff_spy_single_v2
```

---

## Running the spy

Basic execution:

```bash
cd src
./aes_ff_spy_single_v2
```

Specify CPU core and sample count, then redirect stdout into the results directory:

```bash
cd src
./aes_ff_spy_single_v2 --core 2 --samples 10000 > ../results/result.csv
```

### Important note

The current program does **not** support an `--out` option. It prints the measurement matrix to standard output, so you must redirect it to a file as shown above.

During execution you should see lines such as:

```text
[spy] pinned to core 2
[spy] hit_mean=371 miss_mean=528 threshold=449
[spy] mapped ./openssl/libcrypto.so size=3428352, Te0 region [...]
```

---

## Output format

The spy writes a CSV-style matrix where:

- each row corresponds to one probe cache line
- each column corresponds to one plaintext first-byte group

Example header:

```text
probe_offset,pt_0,pt_16,pt_32,...,pt_240
```

Values represent hit counts. Higher values indicate stronger cache reuse.

---

## Visualizing results

The current plotting script is hardcoded to read a file named `result.csv` from the `scripts/` directory.

### Option 1: visualize a new run

```bash
cp results/result.csv scripts/result.csv
cd scripts
python3 plot_spy.py
```

### Option 2: visualize one of the stored sample outputs

```bash
cp results/result_3_17_10000iter_v2.txt scripts/result.csv
cd scripts
python3 plot_spy.py
```

The script writes:

```text
scripts/spy_heatmap.png
```

If you want the reduced figure version as well, keep `spy_heatmap_1000.png` as a stored artifact or update the plotting script accordingly.

---

## Example stored results

The `results/` directory currently contains two sample outputs:

- `result_3_17_10000iter.txt`
- `result_3_17_10000iter_v2.txt`

The second file shows a much cleaner near-diagonal pattern. For example, `pt_0` strongly activates `0x000000`, `pt_16` strongly activates `0x000040`, `pt_32` strongly activates `0x000080`, and so on, with many entries near 9998 to 10000 hits.

This is consistent with a successful Flush+Flush signal on the selected AES T-table region.

---

## Interpretation

- Horizontal structure corresponds to active AES T-table cache lines.
- Variation across plaintext columns reflects input-dependent table lookups.
- A strong near-diagonal pattern indicates that the selected probe region aligns well with the victim lookup pattern.
- The `result_3_17_10000iter_v2.txt` sample is the cleaner reference output among the stored results.

---

## Notes

- Pin the process to a fixed core when possible.
- Reduce background noise for more stable thresholds.
- Disable aggressive CPU frequency scaling if possible.
- Increase `NUMBER_OF_ENCRYPTIONS` or `--samples` for stronger signals.
- If you later change the directory layout again, update both `LIBCRYPTO_PATH` in `src/aes_ff_spy_single_v2.c` and `CSV_FILE` in `scripts/plot_spy.py`.

---


