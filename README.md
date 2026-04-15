# ⚡ Binance Ultra-HFT Tick Processing Engine

![NVIDIA Nsight Profiling](Pasted%20image.png)

## 📖 Overview
This repository contains a high-performance, hardware-optimized I/O and compute infrastructure designed for processing massive tick-level financial data. It achieves extreme throughput by completely overlapping disk I/O with GPU computation using a strict Out-of-Core streaming architecture.

## 📊 Benchmark & Hardware Specifications
The following performance metrics were recorded under strict physical timing (~60 seconds execution time) to ensure absolute memory safety and architectural stability over extended runs. 

**Hardware Environment (Colorful G16 2024):**
*   **CPU:** Intel Core i7-13650HX (14 Cores, 20 Threads)
*   **GPU:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB GDDR6)
*   **RAM:** 16GB DDR5 4800MHz (Strict constraint for Out-of-Core processing)
*   **Storage:** PCIe 3.0 NVMe SSD

**Performance Metrics:**
*   **Dataset:** 139 GB Binance Standard Historical Data
*   **Execution Time:** **~ 60 Seconds** (Physical stopwatch verified)
*   **Throughput:** **~ 2.31 GB/s** (Sustained)
> *Note: While experimental, unsafe builds reached peak throughputs of >3.0 GB/s (~43s execution), this stable version balances extreme I/O extraction with strict memory alignment to prevent `Segfault` during massive data ingestion.*

## ⚙️ Core Architecture

### 1. Zero-Allocation Data Ingestion
Bypasses bloated standard CSV/JSON parsers. Raw byte streams are piped directly from disk into a C++ parser using strict pointer arithmetic. Data is packed into a high-density 25-byte binary `struct` utilizing `__attribute__((packed))`, minimizing memory footprint and CPU parsing overhead.

### 2. Out-of-Core CUDA Compute Pipeline
Overcomes the 16GB RAM physical limitation when processing 139GB of data:
*   **Double Buffering:** Implemented via POSIX `pread` and `std::atomic` flags. A background I/O thread aggressively fetches from the SSD while the main thread pushes the previous chunk to the GPU.
*   **Pinned Memory DMA:** Utilizes `cudaHostAlloc` for zero-copy Host-to-Device transfers over the PCIe bus.
*   **Pipeline Overlapping:** As shown in the Nsight profile above, CPU `pread` and GPU `HtoD / Kernel Execution` are perfectly overlapped on the timeline, effectively hiding disk I/O latency.

### 3. Bare-Metal Low Latency Gateway
For real-time data ingestion, the system bypasses high-level WebSocket libraries in favor of raw TCP sockets (`<sys/socket.h>`) with OpenSSL. Nagle's algorithm is explicitly disabled (`TCP_NODELAY`) to achieve microsecond-level market data latency, coupled with real-time momentum and NetLag feature extraction.

---
*For more details regarding the I/O optimization pipeline and NVIDIA Nsight Systems profiling, please visit my[Upwork Portfolio](https://www.upwork.com/freelancers/~01cd758c8d7cd457a3?p=2014689789157888000).*
