<div align="center">

# ⚡ Binance Ultra-HFT Tick Processing Engine
### Une preuve de concept de performance bare-metal, fortement couplée au matériel

[English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Español](README.es.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

</div>

---

## 🇫🇷 Version française

> ⚠️ **AVERTISSEMENT STRICT ET NATURE DE CE PROJET**
>
> 1. **Portabilité nulle :** Ceci **N'EST PAS** une bibliothèque généraliste. La base de code est fortement hardcodée et strictement couplée à la topologie matérielle spécifique (lignes de cache CPU, largeur du bus PCIe, limites de VRAM) de la machine sur laquelle elle a été développée. **Si vous l'exécutez sur un autre PC, il y a de fortes chances qu'il provoque un Segfault ou subisse une dégradation sévère des performances.**
> 2. **Aucune maintenance :** Il s'agit d'une preuve de concept (PoC) finalisée, construite spécifiquement pour les données tick de Binance. Je ne la maintiendrai pas, ne la mettrai pas à jour et ne fournirai pas de support (sauf si je m'ennuie vraiment énormément).
> 3. **Pas un conseil financier :** Ce moteur ne garantit pas la stabilité d'exécution dans un environnement de trading réel et ne garantit certainement pas la rentabilité. Ne l'utilisez pas avec de l'argent réel.
>
> **Pourquoi l'ouvrir en open source ?** Pour montrer des techniques extrêmes d'optimisation bare-metal en C++/CUDA. Vous pouvez étudier le code source, extraire les paradigmes réseau/calcul et discuter des implémentations techniques.

### Modules et spécifications de performance

Ce projet se compose de deux composants distincts ayant des objectifs de performance différents.

#### Partie 1 : Traitement historique par lots (`data_ingestion/` et `cuda_compute_engine/`)

Il s'agit du moteur principal de throughput, conçu pour parser et calculer d'énormes jeux de données historiques aux limites physiques du matériel. Les benchmarks suivants s'appliquent **uniquement** à ce module.

![NVIDIA Nsight Profiling](README.assets/nsight-profile.png.png)
*(Le `pread` CPU, le DMA PCIe Host-to-Device et l'exécution des kernels GPU sont parfaitement superposés)*

**Matériel cible (optimisé de façon rigide pour cette machine) :**
* **CPU :** Intel Core i7-13650HX (14 cœurs, 20 threads)
* **GPU :** NVIDIA GeForce RTX 4060 Laptop GPU (8GB GDDR6)
* **RAM :** 16GB DDR5 4800MHz (contrainte physique stricte imposant une architecture Out-of-Core)
* **Stockage :** SSD NVMe PCIe 3.0

**Mesures d'exécution :**
* **Jeu de données :** 139 GB de données historiques de trades Spot BTC/USDT de Binance. Le parseur est hardcodé pour ce format CSV spécifique ; l'utiliser pour ETH ou d'autres symboles/types de données **provoquera très probablement des échecs de parsing**.
* **Temps d'exécution :** **~60 secondes** (mesuré physiquement au chronomètre).
* **Throughput soutenu :** **~2.31 GB/s**.
> *Note sur les performances : la version stable en 60 secondes est réglée pour la contrainte de 16GB de RAM. Une build expérimentale, plus extrême, a atteint un throughput de pointe de **~3.23 GB/s** (139GB en 43s, également mesuré au chronomètre), mais sa stratégie agressive de pré-allocation mémoire risque de dépasser la limite des 16GB de RAM physique, entraînant des gels du système.*

#### Partie 2 : Passerelle réseau temps réel (`low_latency_gateway/`)
Ce module ne concerne **pas** le throughput, mais la minimisation de la latence pour l'ingestion de données en temps réel. Il utilise une programmation socket bare-metal pour se connecter à l'API WebSocket de Binance, avec des réglages spécifiques au niveau noyau (`TCP_NODELAY`) afin de garantir que les données de marché soient reçues et traitées en microsecondes. Aucun benchmark de throughput ne s'applique à ce module.

### ⚙️ Détails d'ingénierie extrême
Ce projet abandonne les abstractions modernes de haut niveau au profit de primitives POSIX brutes et de dispositions mémoire strictes.

#### 1. Pipeline d'E/S hardcodé et sans allocation (`data_ingestion/`)
* **Streaming avec contournement du disque :** Les fichiers `.zip` ne sont jamais extraits sur disque. `unzip -p` est parallélisé via `xargs` et pipe directement vers le `stdin` du binaire C++ afin de contourner les limites d'écriture du SSD.
* **Arithmétique de pointeurs fractionnaire :** `convert.cpp` abandonne les conversions standard string-vers-float. Il parse les chiffres ASCII en un seul passage avant, en comptant manuellement les indices fractionnaires (`fraction_count - 8`) afin de sérialiser les prix en entiers (Satoshis/0.01 USDT).
* **Emballage dense :** Les données sont forcées dans une struct de 25 octets avec `__attribute__((packed))`, éliminant le padding du compilateur afin de maximiser le taux de hit du cache L1/L2.

#### 2. Pipeline de calcul CUDA Out-of-Core (`cuda_compute_engine/`)
* **Affinité cache L3 et cœurs :** Le thread d'E/S et les threads de calcul sont strictement épinglés à des cœurs CPU spécifiques avec `pthread_setaffinity_np` afin d'éviter le surcoût des changements de contexte et les migrations imposées par l'ordonnanceur de l'OS.
* **Double buffering sans verrou :** La synchronisation entre la récupération des données côté CPU et leur soumission au GPU est gérée uniquement via des flags `std::atomic`.
* **DMA PCIe zero-copy :** Les allocations RAM contournent la pagination virtuelle grâce à `cudaHostAlloc`. `pread` écrit directement dans la mémoire hôte pinned, qui est ensuite récupérée de manière asynchrone par le GPU via `cudaMemcpyAsync`, masquant entièrement la latence disque.

#### 3. Passerelle réseau bare-metal (`low_latency_gateway/`)
* **Sockets au niveau noyau :** Les bibliothèques WebSocket de haut niveau sont entièrement supprimées. La passerelle utilise directement `<sys/socket.h>` enveloppé avec OpenSSL.
* **Réglage de latence à l'échelle de la microseconde :** La pile TCP est explicitement réglée. L'algorithme de Nagle est désactivé (`TCP_NODELAY`) pour empêcher le système d'exploitation de regrouper les paquets réseau, garantissant ainsi une propagation immédiate des signaux de micro-volatilité.
* **Dépaquetage brut des trames :** Le code parse directement les en-têtes du protocole WebSocket via des opérations bit à bit afin de localiser la charge utile JSON, évitant toute allocation intermédiaire sur le tas de `std::string` dans la hot path.
