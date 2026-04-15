<div align="center">

# ⚡ Binance Ultra-HFT Tick Processing Engine 
### A Bare-Metal, Hardware-Coupled Performance PoC

**Languages:** [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Español](README.es.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [日本語](README.ja.md) | [한국어](README.ko.md)


</div>

---

## 🇺🇸 English Version

> ⚠️ **STRICT DISCLAIMER & NATURE OF THIS PROJECT**
> 
> 1. **Zero Portability:** This is **NOT** a general-purpose library. The codebase is heavily hardcoded and strictly coupled to the specific hardware topology (CPU cache lines, PCIe bus width, VRAM limits) of the machine it was developed on. **If you run this on a different PC, it will likely Segfault or suffer severe degraded performance.**
> 2. **No Maintenance:** This is a finished Proof-of-Concept (PoC) built specifically for Binance tick data. I will not maintain, update, or provide support for it (unless I get extremely bored).
> 3. **Not Financial Advice:** This engine does not guarantee execution stability in a live trading environment and certainly does not guarantee profitability. Do not use this for real money.
> 
> **Why open-source it?** To showcase extreme, bare-metal C++/CUDA optimization techniques. You are welcome to study the source code, extract the network/compute paradigms, and discuss the technical implementations.

### Modules & Performance Specifications

This project consists of two distinct components with different performance goals.

#### Part 1: Historical Batch Processing (`data_ingestion/` & `cuda_compute_engine/`)

This is the core throughput engine, designed to parse and compute massive historical datasets at the physical limits of the hardware. The following benchmarks apply **only** to this module.

![NVIDIA Nsight Profiling](README.assets/nsight-profile.png.png)
*(CPU `pread`, PCIe Host-to-Device DMA, and GPU Kernel execution perfectly overlapped)*

**Target Hardware (Hard-Tuned For):**
*   **CPU:** Intel Core i7-13650HX (14 Cores, 20 Threads)
*   **GPU:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB GDDR6)
*   **RAM:** 16GB DDR5 4800MHz (Strict physical constraint forcing an Out-of-Core architecture)
*   **Storage:** PCIe 3.0 NVMe SSD

**Execution Metrics:**
*   **Dataset:** 139 GB of Binance BTC/USDT Spot Historical Trade Data. The parser is hardcoded for this specific CSV format; using it for ETH or other symbols/data types **will likely cause parsing failures**.
*   **Execution Time:** **~60 Seconds** (Physically timed with a stopwatch).
*   **Sustained Throughput:** **~2.31 GB/s**.
> *Note on performance: The 60-second stable run is tuned for the 16GB RAM constraint. An experimental, more extreme build reached a peak throughput of **~3.23 GB/s** (139GB in 43s, also timed via stopwatch), but its aggressive memory pre-allocation strategy risks overwhelming the limited 16GB of physical RAM, leading to system freezes.*

#### Part 2: Real-Time Network Gateway (`low_latency_gateway/`)
This module is **not** about throughput but about minimizing latency for real-time data ingestion. It uses bare-metal socket programming to connect to Binance's WebSocket API, with specific kernel-level tuning (`TCP_NODELAY`) to ensure market data is received and processed in microseconds. No throughput benchmark is applicable here.

### ⚙️ Extreme Engineering Details
This project discards modern high-level abstractions in favor of raw POSIX primitives and strict memory layouts.

#### 1. Zero-Allocation, Hardcoded I/O Pipeline (`data_ingestion/`)
*   **Disk-Bypass Streaming:** `.zip` files are never extracted to disk. `unzip -p` is parallelized via `xargs` and piped directly into the `stdin` of the C++ binary to bypass SSD write limits.
*   **Fractional Pointer Arithmetic:** `convert.cpp` abandons standard string-to-float conversions. It parses ASCII digits in a single forward pass, manually counting fractional indices (`fraction_count - 8`) to serialize prices into integers (Satoshis/0.01 USDT).
*   **Density Packing:** Data is forced into a 25-byte struct using `__attribute__((packed))`, eliminating compiler padding to maximize L1/L2 cache hit rates.

#### 2. Out-of-Core CUDA Compute Pipeline (`cuda_compute_engine/`)
*   **L3 Cache & Core Pinning:** The I/O thread and compute threads are strictly pinned to specific CPU cores using `pthread_setaffinity_np` to prevent context-switching overhead and OS scheduler migrations.
*   **Lockless Double Buffering:** Synchronization between CPU storage fetch and GPU submission is managed purely by `std::atomic` flags.
*   **Zero-Copy PCIe DMA:** RAM allocations bypass virtual paging using `cudaHostAlloc`. `pread` writes directly into pinned host memory, which is then asynchronously pulled by the GPU via `cudaMemcpyAsync`, entirely hiding disk latency.

#### 3. Bare-Metal Network Gateway (`low_latency_gateway/`)
*   **Kernel-Level Sockets:** High-level WebSocket libraries are completely stripped out. The gateway uses raw `<sys/socket.h>` wrapped in OpenSSL.
*   **Microsecond Latency Tuning:** The TCP stack is explicitly tuned. Nagle's algorithm is disabled (`TCP_NODELAY`) to prevent the OS from batching network packets, ensuring immediate propagation of micro-volatility signals.
*   **Raw Frame Unpacking:** The code directly parses WebSocket protocol headers via bitwise operations to locate the JSON payload, avoiding intermediate `std::string` heap allocations during the hot path.