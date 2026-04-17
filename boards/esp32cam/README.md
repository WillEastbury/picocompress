# ESP32-CAM Benchmark

## Prerequisites

- Arduino IDE or PlatformIO
- Board: **AI Thinker ESP32-CAM** (or any ESP32 board)
- USB serial at **115200 baud**

## Setup

1. Copy `src/picocompress.h` and `src/picocompress.c` into the same folder as `esp32cam_benchmark.ino`
2. Open `esp32cam_benchmark.ino` in Arduino IDE
3. Select board: **AI Thinker ESP32-CAM** (or your ESP32 variant)
4. Upload and open Serial Monitor at 115200

## What it tests

- **Profile:** Q4 (hash 2048×2, history 2048, lazy 2-step)
- **Payloads:** json-508, pattern-508, prose-4K, prose-32K, random-508
- **Metrics:** compress/decompress time (us), throughput (MB/s), ratio, CRC32 verification

## Expected output

```
=== picocompress benchmark — ESP32-CAM ===
CPU freq: 240 MHz
Free heap: XXXXX bytes
Profile: Q4 (b11 d2 h2048 lazy2)

| Payload          |   Size |   Comp | Ratio |  Enc us |  Dec us | Enc MB/s | Dec MB/s | CRC  |
|------------------|--------|--------|-------|---------|---------|----------|----------|------|
| json-508         |    508 |    XXX | X.XXx |    XXXX |    XXXX |   XX.XX  |   XX.XX  | PASS |
| ...              |    ... |    ... |  ...  |     ... |     ... |    ...   |    ...   |  ... |

=== benchmark complete ===
```
