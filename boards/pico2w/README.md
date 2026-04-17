# Pico 2W Benchmark

## Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) installed
- `PICO_SDK_PATH` environment variable set
- `arm-none-eabi-gcc` in PATH
- CMake 3.13+

## Build

```bash
cd boards/pico2w
mkdir build && cd build
cmake -DPICO_BOARD=pico2_w ..
make -j4
```

This produces `picocompress_bench.uf2`.

## Flash

1. Hold BOOTSEL on the Pico 2W and plug in USB
2. Drag `picocompress_bench.uf2` to the RPI-RP2 drive
3. Open a serial terminal at 115200 baud (the Pico appears as a USB CDC device)

On Windows:
```powershell
# Find the COM port in Device Manager, then:
putty -serial COM3 -sercfg 115200
```

On Linux/Mac:
```bash
screen /dev/ttyACM0 115200
```

## What it tests

- **Profile:** Q4 (hash 2048×2, history 2048, lazy 2-step)
- **Payloads:** json-508, pattern-508, prose-4K, prose-32K, random-508
- **Metrics:** compress/decompress time (us), throughput (MB/s), ratio, CRC32 verification
- **HW acceleration:** reports whether CRC32 hash and CLZ match paths are active

## Expected output

```
=== picocompress benchmark — Pico 2W (RP2350) ===
CPU freq: 150 MHz
Profile: Q4 (b11 d2 h2048 lazy2)
HW accel: CRC32 hash ON
HW accel: CLZ word-at-a-time match ON

| Payload          |   Size |   Comp | Ratio |  Enc us |  Dec us | Enc MB/s | Dec MB/s | CRC  |
|------------------|--------|--------|-------|---------|---------|----------|----------|------|
| json-508         |    508 |    XXX | X.XXx |    XXXX |    XXXX |   XX.XX  |   XX.XX  | PASS |
| ...              |    ... |    ... |  ...  |     ... |     ... |    ...   |    ...   |  ... |

=== benchmark complete ===
```

## Notes

- The RP2350's Cortex-M33 supports CRC32 instructions (with `-march=armv8-m.main+crc`) and CLZ for word-at-a-time matching
- If CRC32 shows as OFF, ensure your toolchain passes `-march=armv8-m.main+crc` to GCC
- The benchmark uses `time_us_64()` for microsecond-accurate timing
