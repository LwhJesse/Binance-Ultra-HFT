<div align="center">

# ⚡ Binance Ultra-HFT Tick Processing Engine
### Una prueba de concepto de rendimiento bare-metal, fuertemente acoplada al hardware

[English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Español](README.es.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

</div>

---

## 🇪🇸 Versión en Español

> ⚠️ **DESCARGO ESTRICTO Y NATURALEZA DE ESTE PROYECTO**
>
> 1. **Portabilidad nula:** Esto **NO** es una biblioteca de propósito general. La base de código está fuertemente hardcodeada y estrictamente acoplada a la topología de hardware específica (líneas de caché de CPU, ancho de bus PCIe, límites de VRAM) de la máquina en la que fue desarrollada. **Si ejecutas esto en otro PC, es muy probable que produzca un Segfault o sufra una degradación severa del rendimiento.**
> 2. **Sin mantenimiento:** Esta es una prueba de concepto (PoC) terminada, construida específicamente para datos tick de Binance. No voy a mantenerla, actualizarla ni ofrecer soporte para ella (a menos que me aburra muchísimo).
> 3. **No es asesoramiento financiero:** Este motor no garantiza estabilidad de ejecución en un entorno de trading en vivo y, desde luego, tampoco garantiza rentabilidad. No lo uses con dinero real.
>
> **¿Por qué publicarlo como open source?** Para mostrar técnicas extremas de optimización bare-metal en C++/CUDA. Puedes estudiar el código fuente, extraer los paradigmas de red/cómputo y debatir las implementaciones técnicas.

### Módulos y especificaciones de rendimiento

Este proyecto consta de dos componentes distintos con objetivos de rendimiento diferentes.

#### Parte 1: Procesamiento histórico por lotes (`data_ingestion/` y `cuda_compute_engine/`)

Este es el motor central de throughput, diseñado para analizar y computar conjuntos masivos de datos históricos en los límites físicos del hardware. Los siguientes benchmarks se aplican **solo** a este módulo.

![NVIDIA Nsight Profiling](nsight-profile.png.png)
*(La ejecución de `pread` en CPU, el DMA PCIe de Host a Device y el Kernel de GPU se solapan perfectamente)*

**Hardware objetivo (ajustado de forma rígida para este sistema):**
* **CPU:** Intel Core i7-13650HX (14 núcleos, 20 hilos)
* **GPU:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB GDDR6)
* **RAM:** 16GB DDR5 4800MHz (restricción física estricta que obliga a una arquitectura Out-of-Core)
* **Almacenamiento:** SSD NVMe PCIe 3.0

**Métricas de ejecución:**
* **Conjunto de datos:** 139 GB de datos históricos de trades Spot BTC/USDT de Binance. El parser está hardcodeado para este formato CSV específico; usarlo con ETH u otros símbolos/tipos de datos **probablemente cause fallos de parsing**.
* **Tiempo de ejecución:** **~60 segundos** (medido físicamente con cronómetro).
* **Throughput sostenido:** **~2.31 GB/s**.
> *Nota sobre el rendimiento: la ejecución estable de 60 segundos está ajustada para la restricción de 16GB de RAM. Una compilación experimental, más extrema, alcanzó un throughput pico de **~3.23 GB/s** (139GB en 43s, también medido con cronómetro), pero su agresiva estrategia de preasignación de memoria puede sobrecargar los limitados 16GB de RAM física y provocar congelamientos del sistema.*

#### Parte 2: Gateway de red en tiempo real (`low_latency_gateway/`)
Este módulo **no** trata sobre throughput, sino sobre minimizar la latencia para la ingesta de datos en tiempo real. Usa programación de sockets bare-metal para conectarse a la API WebSocket de Binance, con ajustes específicos a nivel de kernel (`TCP_NODELAY`) para garantizar que los datos de mercado se reciban y procesen en microsegundos. No corresponde ningún benchmark de throughput para este módulo.

### ⚙️ Detalles de ingeniería extrema
Este proyecto descarta las abstracciones modernas de alto nivel en favor de primitivas POSIX crudas y diseños de memoria estrictos.

#### 1. Pipeline de E/S hardcodeado y sin asignaciones (`data_ingestion/`)
* **Streaming con bypass de disco:** Los archivos `.zip` nunca se extraen a disco. `unzip -p` se paraleliza mediante `xargs` y se canaliza directamente al `stdin` del binario C++ para evitar los límites de escritura del SSD.
* **Aritmética de punteros fraccional:** `convert.cpp` abandona las conversiones estándar de string a float. Analiza los dígitos ASCII en una sola pasada hacia adelante, contando manualmente los índices fraccionales (`fraction_count - 8`) para serializar los precios en enteros (Satoshis/0.01 USDT).
* **Empaquetado de densidad:** Los datos se fuerzan dentro de una struct de 25 bytes usando `__attribute__((packed))`, eliminando el padding del compilador para maximizar la tasa de aciertos en caché L1/L2.

#### 2. Pipeline de cómputo CUDA Out-of-Core (`cuda_compute_engine/`)
* **Fijación a caché L3 y afinidad de núcleos:** El hilo de E/S y los hilos de cómputo se fijan estrictamente a núcleos específicos de CPU usando `pthread_setaffinity_np` para evitar la sobrecarga de cambios de contexto y migraciones del scheduler del sistema operativo.
* **Doble buffering sin locks:** La sincronización entre la obtención de datos en CPU y el envío a la GPU se gestiona puramente mediante flags `std::atomic`.
* **DMA PCIe de copia cero:** Las asignaciones de RAM evitan la paginación virtual usando `cudaHostAlloc`. `pread` escribe directamente en memoria host pinned, que luego es transferida asíncronamente por la GPU mediante `cudaMemcpyAsync`, ocultando por completo la latencia de disco.

#### 3. Gateway de red bare-metal (`low_latency_gateway/`)
* **Sockets a nivel de kernel:** Las bibliotecas WebSocket de alto nivel se eliminan por completo. El gateway usa `<sys/socket.h>` crudo, encapsulado con OpenSSL.
* **Ajuste de latencia a microsegundos:** La pila TCP se ajusta explícitamente. El algoritmo de Nagle se desactiva (`TCP_NODELAY`) para evitar que el sistema operativo agrupe paquetes de red, asegurando la propagación inmediata de señales de microvolatilidad.
* **Desempaquetado crudo de frames:** El código analiza directamente los headers del protocolo WebSocket mediante operaciones bit a bit para localizar la carga útil JSON, evitando asignaciones intermedias en heap de `std::string` en la hot path.
