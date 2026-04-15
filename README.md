# ⚡ Binance Ultra-HFT Backtest Engine (Abandoned)

**[WARNING] This is NOT a general-purpose backtesting library. This is an extreme, hardware-specific I/O and compute infrastructure experiment.**

This project was built to explore the absolute limits of consumer-grade hardware (PCIe 3.0 NVMe SSD + 16GB RAM + Specific NVIDIA GPU Architecture) in processing massive tick-level financial data.

### 📊 Performance Record
- **Dataset:** 139 GB Binance Standard Historical Data
- **Execution Time:** **~ 43 Seconds**
- **Throughput:** **> 3.23 GB/s** (Maxed out the physical read limit of PCIe 3.0 NVMe SSD)

### 🛠️ Hardcore Engineering Trade-offs
To achieve nanosecond-level optimization and Zero-Overhead parsing, this system embraces extreme design compromises:
1. **Zero Fault Tolerance (Strict Schema):** Abandons all boundary checks and exception handling. Parses raw bytes via pointer arithmetic. A single byte misalignment in the source data will result in an immediate `Segfault`.
2. **Out-of-Core Streaming:** Implemented Double Buffering pipeline via POSIX `pread` and CUDA Pinned Memory (`cudaHostAlloc`) to hide Disk I/O latency completely within a 16GB RAM constraint.
3. **Bare-Metal Networking & Micro-features:** Bypasses bloated high-level WebSocket libraries. Implements raw TCP sockets with OpenSSL (`TCP_NODELAY`) for microsecond-level market data ingestion, coupled with real-time momentum and NetLag feature extraction.

**Status:**
The project is suspended at the infrastructure layer. It is open-sourced under the **AGPL-3.0 License** solely as a Reference Implementation for high-performance systems engineering (C/CUDA).
