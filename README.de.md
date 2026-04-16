<div align="center">

# ⚡ Binance Ultra-HFT Tick Processing Engine
### Ein Bare-Metal-Performance-PoC mit harter Hardware-Kopplung

[English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Español](README.es.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

</div>

---

## 🇩🇪 Deutsche Version

> ⚠️ **STRENGER HAFTUNGSAUSSCHLUSS & CHARAKTER DIESES PROJEKTS**
>
> 1. **Null Portabilität:** Dies ist **KEINE** Allzweckbibliothek. Die Codebasis ist stark hardcodiert und strikt an die spezifische Hardware-Topologie (CPU-Cache-Lines, PCIe-Busbreite, VRAM-Grenzen) des Systems gekoppelt, auf dem sie entwickelt wurde. **Wenn du das auf einem anderen PC ausführst, wird es mit hoher Wahrscheinlichkeit zu einem Segfault oder zu massivem Performanceverlust kommen.**
> 2. **Keine Wartung:** Dies ist ein abgeschlossenes Proof-of-Concept (PoC), das speziell für Binance-Tickdaten gebaut wurde. Ich werde es nicht warten, aktualisieren oder Support dafür leisten (es sei denn, mir ist extrem langweilig).
> 3. **Keine Finanzberatung:** Diese Engine garantiert weder Ausführungsstabilität in einer Live-Trading-Umgebung noch Profitabilität. Verwende sie nicht mit echtem Geld.
>
> **Warum Open Source?** Um extreme Bare-Metal-Optimierungstechniken in C++/CUDA zu demonstrieren. Du kannst den Quellcode studieren, die Netzwerk-/Compute-Paradigmen herausziehen und die technischen Implementierungen diskutieren.

### Module und Leistungsspezifikationen

Dieses Projekt besteht aus zwei unterschiedlichen Komponenten mit verschiedenen Leistungszielen.

#### Teil 1: Historische Batch-Verarbeitung (`data_ingestion/` & `cuda_compute_engine/`)

Dies ist die zentrale Throughput-Engine, die dafür ausgelegt ist, riesige historische Datensätze an den physikalischen Grenzen der Hardware zu parsen und zu berechnen. Die folgenden Benchmarks gelten **nur** für dieses Modul.

![NVIDIA Nsight Profiling](README.assets/nsight-profile.png.png)
*(CPU-`pread`, PCIe Host-to-Device DMA und GPU-Kernel-Ausführung sind perfekt überlappt)*

**Zielhardware (hart auf dieses System abgestimmt):**
* **CPU:** Intel Core i7-13650HX (14 Kerne, 20 Threads)
* **GPU:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB GDDR6)
* **RAM:** 16GB DDR5 4800MHz (strikte physische Begrenzung, die eine Out-of-Core-Architektur erzwingt)
* **Storage:** PCIe 3.0 NVMe SSD

**Ausführungsmetriken:**
* **Datensatz:** 139 GB historischer Binance BTC/USDT Spot-Trade-Daten. Der Parser ist für dieses spezifische CSV-Format hardcodiert; bei Nutzung für ETH oder andere Symbole/Datentypen **kommt es sehr wahrscheinlich zu Parsing-Fehlern**.
* **Ausführungszeit:** **~60 Sekunden** (physisch mit einer Stoppuhr gemessen).
* **Nachhaltiger Throughput:** **~2.31 GB/s**.
> *Hinweis zur Performance: Die stabile 60-Sekunden-Ausführung ist auf die 16GB-RAM-Beschränkung abgestimmt. Ein experimenteller, aggressiverer Build erreichte einen Spitzen-Throughput von **~3.23 GB/s** (139GB in 43s, ebenfalls per Stoppuhr gemessen), aber seine aggressive Speicher-Vorallokationsstrategie kann die begrenzten 16GB physischen RAM überfordern und zu System-Freezes führen.*

#### Teil 2: Echtzeit-Netzwerk-Gateway (`low_latency_gateway/`)
Bei diesem Modul geht es **nicht** um Throughput, sondern um die Minimierung der Latenz für die Echtzeit-Datenaufnahme. Es verwendet Bare-Metal-Socket-Programmierung, um sich mit der WebSocket-API von Binance zu verbinden, sowie spezifisches Kernel-Level-Tuning (`TCP_NODELAY`), damit Marktdaten in Mikrosekunden empfangen und verarbeitet werden. Für dieses Modul ist kein Throughput-Benchmark anwendbar.

### ⚙️ Extreme Engineering-Details
Dieses Projekt verwirft moderne High-Level-Abstraktionen zugunsten roher POSIX-Primitiven und strikter Speicherlayouts.

#### 1. Hardcodierte Zero-Allocation-I/O-Pipeline (`data_ingestion/`)
* **Disk-Bypass-Streaming:** `.zip`-Dateien werden niemals auf die Festplatte entpackt. `unzip -p` wird über `xargs` parallelisiert und direkt in die `stdin` der C++-Binärdatei geleitet, um die Schreiblimits der SSD zu umgehen.
* **Manuelle Pointer-Arithmetik für Nachkommastellen:** `convert.cpp` verzichtet auf standardmäßige String-zu-Fließkomma-Konvertierungen. Es parst ASCII-Ziffern in einem einzigen Durchlauf und zählt manuell die Nachkommastellen (`fraction_count - 8`), um Preise als Ganzzahlen (Satoshis/0,01 USDT) zu serialisieren.
* **Dichtes Packen:** Daten werden mittels `__attribute__((packed))` in eine 25-Byte-Struktur gezwungen, wodurch das Compiler-Padding eliminiert wird, um die Trefferquoten im L1/L2-Cache zu maximieren.

#### 2. Out-of-Core CUDA Compute-Pipeline (`cuda_compute_engine/`)
* **L3-Cache & Core-Pinning:** Der I/O-Thread und die Compute-Threads werden mittels `pthread_setaffinity_np` strikt an spezifische CPU-Kerne gebunden, um den Overhead durch Kontextwechsel und Migrationen durch den OS-Scheduler zu verhindern.
* **Sperrfreies Double-Buffering:** Die Synchronisation zwischen dem Abrufen von Daten vom Speicher durch die CPU und der Übermittlung an die GPU wird ausschließlich über `std::atomic`-Flags gesteuert.
* **Zero-Copy PCIe DMA:** RAM-Allokationen umgehen das virtuelle Paging durch die Verwendung von `cudaHostAlloc`. `pread` schreibt direkt in den fixierten Host-Speicher (Pinned Memory), der dann asynchron von der GPU über `cudaMemcpyAsync` abgerufen wird, wodurch die Festplattenlatenz vollständig verdeckt wird.

#### 3. Bare-Metal-Netzwerk-Gateway (`low_latency_gateway/`)
* **Kernel-Level-Sockets:** Hochrangige WebSocket-Bibliotheken werden komplett entfernt. Das Gateway verwendet rohe `<sys/socket.h>`-Aufrufe, die in OpenSSL verpackt sind.
* **Latenz-Tuning im Mikrosekundenbereich:** Der TCP-Stack wird explizit optimiert. Der Nagle-Algorithmus wird deaktiviert (`TCP_NODELAY`), um zu verhindern, dass das Betriebssystem Netzwerkpakete bündelt, wodurch die sofortige Übertragung von Signalen mit Mikrovolatilität sichergestellt wird.
* **Rohes Entpacken von Frames:** Der Code parst die Header des WebSocket-Protokolls direkt über bitweise Operationen, um die JSON-Nutzdaten zu lokalisieren. Dadurch werden Zwischenallokationen von `std::string` auf dem Heap im kritischen Pfad (Hot Path) vermieden.
