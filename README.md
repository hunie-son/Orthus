# Orthus PoC

This directory contains the proof-of-concept artifacts used in our evaluation.

## Layout

- `BM1/`  
  Plaintext Intel TDX baseline benchmark.

- `BM2/`  
  Software-only FHE baseline with native CKKS bootstrapping.

- `BM3/`  
  Intel TDX-assisted FHE benchmark with ciphertext refresh inside a trusted domain.

- `TDX-Flush-Flush-PoC/`  
  Flush+Flush side-channel proof-of-concept suite for demonstration purposes.  
  This PoC is inspired by the Flush+Flush methodology demonstrated in TDXploit [1].  
  The experiments are configured to evaluate Flush+Flush behavior in a TDX-based setting.

## Dataset

For MNIST-based experiments, the dataset can be downloaded by running the provided
`get_mnist.py` script in the corresponding benchmark directory:

```bash
# BM1
python3 BM1/TDX_only_Unified_TDXVM/host/data/get_mnist.py

# BM2
python3 BM2/FHE_only_Unified_OpenMP_MT_final/data/get_mnist.py

# BM3
python3 BM3/TDX_FHE_Unified_TDXVM_OpenMP_MT_final_v2/data/get_mnist.py
```

Each benchmark keeps its own local copy of the dataset under its corresponding
`data/` directory.

## Requirements

- `BM1/` requires Intel TDX hardware and a properly configured TDX software stack.
- `BM2/` requires the OpenFHE library.
- `BM3/` requires Intel TDX hardware and a properly configured TDX software stack.
  Depending on the build configuration, OpenFHE may also be required for the FHE components.

Please note that Intel TDX setup is platform-specific and requires hardware,
firmware, BIOS, kernel, and virtualization support. We do not provide support for
configuring Intel TDX environments.

If needed, we can provide a remote demonstration of the artifacts in our configured
experimental environment.

## Note

Each subdirectory has its own README with detailed build, run, and result information.

## Reference

[1] Fabian Rauscher, Luca Wilke, Hannes Weissteiner, Thomas Eisenbarth, and Daniel Gruss.
"TDXploit: Novel Techniques for Single-Stepping and Cache Attacks on Intel TDX."
34th USENIX Security Symposium, 2025.
