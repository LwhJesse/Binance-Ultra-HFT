# ⚡ Binance Ultra-HFT Backtest Engine (Abandoned)

**[WARNING] This is NOT a general-purpose backtesting library. This is an extreme, hardware-specific I/O and compute infrastructure experiment.**

This project was built to explore the absolute limits of consumer-grade hardware in processing massive tick-level financial data. It demonstrates how to maximize PCIe NVMe SSD throughput and overlap disk I/O with CUDA compute on a memory-constrained machine.

### 💻 Hardware Environment
The benchmarks were physical-stopwatch verified on a consumer gaming laptop (**Colorfire G16, 2024 Edition**):
- **CPU:** Intel Core i7-13650HX
- **RAM:** 16GB DDR5 (Strict memory constraint for Out-of-Core processing)
- **Storage:** PCIe NVMe SSD

### 📊 Performance Record (Stable Version)
- **Dataset:** 139 GB Binance Standard Historical Data (2017 - 2025.11)
- **Execution Time:** **~ 60 Seconds** (Physical stopwatch)
- **Throughput:** **~ 2.31 GB/s**
> *Note: An earlier, highly experimental version achieved ~43 seconds (>3.2 GB/s), but it was overly brittle and lacked memory safety. This repository contains the refactored, stable version that balances extreme I/O extraction with architectural reliability.*

### 🛠️ Hardcore Engineering Trade-offs
To achieve nanosecond-level optimization and Zero-Overhead parsing, this system embraces extreme design compromises:
1. **Zero Fault Tolerance (Strict Schema):** Abandons all boundary checks and exception handling. Parses raw bytes via pointer arithmetic. A single byte misalignment in the source data will result in an immediate `Segfault`.
2. **Out-of-Core Streaming (Double Buffering):** Implemented a double-buffering pipeline via POSIX `pread` and CUDA Pinned Memory (`cudaHostAlloc`) to hide Disk I/O latency completely within a 16GB RAM constraint.
3. **Bare-Metal Networking:** Bypasses bloated high-level WebSocket libraries. Implements raw TCP sockets with OpenSSL, disabling Nagle's algorithm (`TCP_NODELAY`) for microsecond-level market data ingestion.

### 📈 Profiling & Upwork Portfolio
This project's I/O optimization pipeline and NVIDIA Nsight Systems (nsys) profiling results have been featured on my Upwork portfolio. 
The Nsight timeline proves the textbook-level overlapping of CPU `pread` disk fetching and GPU `HtoD / sum_data` kernel execution.
🔗 **[View the Nsight Profiling Showcase on my Upwork](https://www.upwork.com/freelancers/~01cd758c8d7cd457a3?p=2014689789157888000)**

**Status:**
The project is suspended at the infrastructure layer. It is open-sourced under the **AGPL-3.0 License** solely as a Reference Implementation for high-performance systems engineering (C/CUDA).
