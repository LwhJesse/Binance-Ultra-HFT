#!/bin/bash
# High-throughput data decompression and parsing pipeline.
# Bypasses disk I/O by streaming unzipped data directly into the C++ parser via UNIX pipes.

THREADS=14
IN_DIR="/run/media/jesse/android/kline"
OUT_DIR="/run/media/jesse/android/kline/bin"
CONVERT="./convert"

mkdir -p "$OUT_DIR"

ls "$IN_DIR"/*.zip | xargs -P $THREADS -I {} sh -c '
    zip_file="{}"
    bin_name=$(basename "$zip_file" .zip).bin
    echo "[INFO] Streaming and parsing: $bin_name"
    unzip -p "$zip_file" | "$CONVERT" > "$OUT_DIR/$bin_name"
'
