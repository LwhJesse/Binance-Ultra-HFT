<div align="center">

# ⚡ Binance Ultra-HFT Tick Processing Engine
### 一個裸機級、與硬體強耦合的效能概念驗證模型

[English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Español](README.es.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

</div>

---

## 🇹🇼 中文版（繁體）

> ⚠️ **嚴正聲明與專案定性**
>
> 1. **毫無通用性（Zero Portability）：** 這**絕對不是**一個通用型基礎函式庫。整個程式碼庫充滿了硬編碼（Hardcode），並且與開發機的硬體拓撲（CPU 快取線、PCIe 匯流排頻寬、顯存容量）進行了**極端強耦合**。**如果你在其他配置的電腦上執行，極高機率會直接 Segfault，或是效能大幅暴跌。**
> 2. **拒絕維護：** 這是一個專為幣安（Binance）Tick 資料客製、且已完成的效能概念驗證（PoC）模型。我沒有精力，也不會對它進行後續維護與更新（除非我真的閒到發慌）。
> 3. **不提供任何獲利保證：** 本引擎不保證在真實實盤環境中的執行穩定性，更不保證任何交易策略的獲利能力。**請勿用於真實資金交易。**
>
> **為什麼要開源？** 只是為了展示 C++ 與 CUDA 在裸機級別（Bare-metal）的極限最佳化技術。歡迎各路開發者研究原始碼、抽取底層網路/異質計算的技術範式，並交流具體實作。

### 模組與效能剖析

本專案由兩個效能目標完全不同的獨立模組構成。

#### 模組一：歷史資料批次處理（`data_ingestion/` 與 `cuda_compute_engine/`）

這是本專案的核心吞吐量引擎，專門為了在硬體物理極限下解析並計算海量歷史資料集而設計。以下所有效能數據**僅適用於此模組**。

![NVIDIA Nsight Profiling](README.assets/nsight-profile.png.png)
*（圖中顯示 CPU `pread`、PCIe Host-to-Device DMA 與 GPU Kernel 執行被完美重疊，以隱藏 I/O 延遲）*

**目標硬體（已針對此配置進行硬編碼最佳化）：**
* **CPU：** Intel Core i7-13650HX（14 核心，20 執行緒）
* **GPU：** NVIDIA GeForce RTX 4060 Laptop GPU（8GB GDDR6）
* **RAM：** 16GB DDR5 4800MHz（由於實體記憶體嚴格受限，系統被迫採用 Out-of-Core 架構）
* **Storage：** PCIe 3.0 NVMe SSD

**執行效能指標：**
* **處理資料集：** 139 GB 的幣安 BTC/USDT **現貨**歷史成交資料。解析器是為這種特定 CSV 格式硬編碼的，若拿去處理 ETH 或其他交易對/資料類型，**極有可能導致解析失敗**。
* **執行耗時：** **約 60 秒**（以實體碼表實測）。
* **持續吞吐量：** **約 2.31 GB/s**。
> *效能說明：這個穩定版的 60 秒執行結果，是針對 16GB RAM 限制所調整的配置。更激進的實驗版曾達到 **約 3.23 GB/s** 的峰值吞吐（139GB 資料、43 秒，同樣以碼表計時），但其激進的記憶體預配置策略可能超出 16GB 實體記憶體的承受上限，導致系統凍結。*

#### 模組二：即時網路閘道（`low_latency_gateway/`）
此模組的效能目標**不是吞吐量，而是極致低延遲**。它以裸機 Socket 程式設計連接幣安 WebSocket API，並透過核心層級的網路調校（`TCP_NODELAY`）確保市場資料能以微秒級延遲被接收與處理。吞吐量指標不適用於此模組。

### ⚙️ 極限工程細節
本專案捨棄了大多數現代高階抽象，直接採用原始 POSIX primitive 與嚴格的記憶體布局。

#### 1. 零配置、硬編碼的 I/O 管線（`data_ingestion/`）
* **繞過磁碟的串流處理：** `.zip` 檔案永遠不落地解壓。透過 `xargs` 平行化 `unzip -p`，再將位元組流直接 pipe 到 C++ 二進位程式的 `stdin`，以繞過 SSD 寫入瓶頸。
* **手動小數位指標運算：** `convert.cpp` 完全捨棄標準字串轉浮點函式。它以單次前向遍歷解析 ASCII 數字，手動計算小數位偏移（`fraction_count - 8`），將價格序列化為整數（Satoshis/0.01 USDT）。
* **高密度封裝：** 使用 `__attribute__((packed))` 關閉編譯器對齊填充，將資料強制壓入 25-byte struct，以最大化 CPU L1/L2 快取命中率。

#### 2. Out-of-Core CUDA 計算管線（`cuda_compute_engine/`）
* **L3 快取與核心綁定：** 透過 `pthread_setaffinity_np` 將 I/O 執行緒與計算執行緒嚴格綁定到特定 CPU 核心，以避免 context switch 開銷與 OS scheduler 遷移。
* **無鎖雙緩衝：** CPU 資料擷取與 GPU 任務提交之間的同步完全依賴 `std::atomic` 旗標來完成，不使用任何 mutex。
* **零拷貝 PCIe DMA：** 使用 `cudaHostAlloc` 申請 pinned host memory，避開虛擬分頁。`pread` 直接把資料寫入該區域，之後 GPU 再透過 `cudaMemcpyAsync` 非同步拉取，完整掩蓋磁碟延遲。

#### 3. 裸機極速網路閘道（`low_latency_gateway/`）
* **核心層級 Socket：** 徹底移除所有高階 WebSocket 第三方函式庫。閘道直接使用原始 `<sys/socket.h>`，並以 OpenSSL 封裝。
* **微秒級延遲微調：** 明確調整 TCP 協定棧。透過啟用 `TCP_NODELAY` 停用 Nagle 演算法，防止作業系統合併小型封包，確保微波動訊號立即傳遞。
* **原始幀解包：** 程式碼透過位元運算直接解析 WebSocket 協定標頭，定位 JSON payload，避免 hot path 上出現任何 `std::string` 的中介 heap 配置。
