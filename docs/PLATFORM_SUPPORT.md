# Platform Support & Hardware Acceleration

picocompress uses **conditional compilation** to exploit hardware acceleration
when available, with a portable C fallback that runs everywhere.

## Target platforms

| Board | CPU | Arch | RAM | Profile | HW acceleration |
|---|---|---|---|---|---|
| Arduino Uno/Nano | ATmega328P | AVR 8-bit | 2K SRAM | Micro | Portable only (8-bit ALU) |
| Arduino Mega | ATmega2560 | AVR 8-bit | 8K SRAM | Minimal | Portable only (8-bit ALU) |
| ESP32 | Xtensa LX6 | 32-bit dual | 520K SRAM | Q4 | HW multiply, no ARM SIMD |
| ESP32-S3 | Xtensa LX7 | 32-bit dual | 512K SRAM | Q4 | Vector-like extensions |
| ESP32-C3 | RISC-V RV32 | 32-bit single | 400K SRAM | Q3 | No vector ext |
| **Pico W** | RP2040 | **Cortex-M0+ dual** | 264K SRAM | Balanced | No unaligned, no barrel shifter |
| **Pico 2W** | RP2350 | **Cortex-M33 dual** | 520K SRAM | Q4 | CRC32 HW hash, CLZ, unaligned |
| **Pi 3B+** | BCM2837B0 | **Cortex-A53 quad** | 1G | Q4 | NEON 16B match, CRC32 hash |
| **Pi 4/400** | BCM2711 | **Cortex-A72 quad** | 1-8G | Q4 | NEON 16B match, CRC32 hash, OoO |
| **Pi 5** | BCM2712 | **Cortex-A76 quad** | 4-8G | Q4 | NEON 16B match, CRC32 hash, big caches |

### Build examples

```sh
# Arduino (AVR) — Micro profile
avr-gcc -mmcu=atmega328p -Os \
    -DPC_BLOCK_SIZE=192u -DPC_HASH_BITS=8u \
    -DPC_HASH_CHAIN_DEPTH=1u -DPC_HISTORY_SIZE=64u \
    picocompress.c ...

# ESP32 (Xtensa) — Q4 profile
xtensa-esp32-elf-gcc -O2 \
    -DPC_HASH_BITS=11u -DPC_HASH_CHAIN_DEPTH=2u \
    -DPC_HISTORY_SIZE=2048u -DPC_LAZY_STEPS=2u \
    picocompress.c ...

# Pico W (RP2040, Cortex-M0+) — Balanced default
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 \
    picocompress.c ...

# Pico 2W (RP2350, Cortex-M33) — Q4 + CRC32 hash + CLZ match
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 -march=armv8-m.main+crc \
    -DPC_HASH_BITS=11u -DPC_HASH_CHAIN_DEPTH=2u \
    -DPC_HISTORY_SIZE=2048u -DPC_LAZY_STEPS=2u \
    picocompress.c ...

# Pi 3/4/5 (AArch64) — Q4 + NEON + CRC32
aarch64-linux-gnu-gcc -O2 -march=armv8-a+crc \
    -DPC_HASH_BITS=11u -DPC_HASH_CHAIN_DEPTH=2u \
    -DPC_HISTORY_SIZE=2048u -DPC_LAZY_STEPS=2u \
    picocompress.c ...
```

## Capability detection

All detection is private to `picocompress.c` — the public API (`picocompress.h`)
is platform-agnostic. Capabilities are auto-detected from compiler predefined
macros and can be individually disabled with `-DPC_NO_*` flags.

| Capability | Macro | Detected via | Target |
|---|---|---|---|
| Hardware CRC32 hash | `PC_HAS_HW_CRC32` | `__ARM_FEATURE_CRC32` | Cortex-M33+, A-class +crc |
| NEON SIMD match | `PC_HAS_NEON` | `__ARM_NEON` | A-class, AArch64 |
| Helium/MVE match | `PC_HAS_MVE` | `__ARM_FEATURE_MVE` | Cortex-M55+ |
| RISC-V Vector match | `PC_HAS_RVV` | `__riscv_vector` | RV32/64 with V extension |
| CLZ/CTZ bit-scan | `PC_HAS_BITSCAN` | `__builtin_ctz` / `_BitScanForward` | GCC/Clang/MSVC |
| Unaligned word loads | `PC_CAN_UNALIGNED` | `__ARM_ARCH >= 7`, x86, AArch64 | M3+, x86, AArch64 |

### Override flags

Disable any accelerated path at compile time:

```sh
# Force portable hash (no CRC32 even if available)
gcc -DPC_NO_HW_CRC32 ...

# Force byte-at-a-time match (no SIMD, no CLZ)
gcc -DPC_NO_NEON -DPC_NO_MVE -DPC_NO_RVV -DPC_NO_BITSCAN ...

# Force byte-at-a-time match but allow CRC32 hash
gcc -DPC_NO_NEON -DPC_NO_BITSCAN ...

# Disable unaligned loads (e.g. for M0 cross-compile testing)
gcc -DPC_NO_UNALIGNED ...
```

## Accelerated code paths

### Hash function (`pc_hash3`)

| Path | Guard | Description |
|---|---|---|
| **Hardware CRC32** | `PC_HAS_HW_CRC32` | 3× `__crc32b` — 1 cycle each on M33+ |
| **Portable multiply** | fallback | `p[0]*251 + p[1]*11 + p[2]*3` — 3 multiplies |

The hash function is encoder-only. Different hash implementations produce
different compressed output, but **any decoder can decompress any stream** —
the wire format is identical regardless of hash function.

### Match length comparison (`pc_match_len`)

Priority cascade (first matching path wins):

| Priority | Path | Guard | Bytes/iter | Mismatch detection |
|---|---|---|---|---|
| 1 | **NEON** | `PC_HAS_NEON` | 16 | `__builtin_ctzll` on XOR mask |
| 2 | **Helium/MVE** | `PC_HAS_MVE` | 16 | `__builtin_ctz` on inverted predicate |
| 3 | **RISC-V Vector** | `PC_HAS_RVV` | VLEN/8 | `vfirst` on not-equal mask |
| 4 | **CLZ word-at-a-time** | `PC_HAS_BITSCAN && PC_CAN_UNALIGNED` | 4 | `__builtin_ctz` on XOR word |
| 5 | **Portable byte loop** | fallback | 1 | byte compare |

All paths produce identical match lengths — only speed differs.

**Safety notes:**
- Word-at-a-time uses `memcpy` for loads (no aliasing violations)
- All wide paths have scalar tail loops for remaining bytes
- NEON/MVE/RVV never read past the `limit` boundary
- CLZ vs CTZ is selected based on `__BYTE_ORDER__` for endianness correctness

## Cortex-M0 minimum viable profile

Cortex-M0 has no barrel shifter, no unaligned loads, and some variants lack
hardware multiply. picocompress is safe on M0 with the portable fallback:

```sh
arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb -Os \
    -DPC_BLOCK_SIZE=192u -DPC_HASH_BITS=8u \
    -DPC_HASH_CHAIN_DEPTH=1u -DPC_HISTORY_SIZE=64u \
    picocompress.c ...
```

| Profile | Enc RAM | Dec RAM | Notes |
|---|---|---|---|
| Micro (`b192 b8 d1 h64`) | ~1.0 KB | ~0.5 KB | Fits 2K SRAM |
| Ultra-micro (`b128 b7 d1 h32`) | ~0.6 KB | ~0.3 KB | Fits 1K SRAM (experimental) |

**M0 safety guarantees:**
- `PC_CAN_UNALIGNED` is `0` → word-at-a-time path is never selected
- `PC_HAS_NEON/MVE/RVV` are all `0` → SIMD paths are never selected
- Only the portable byte-at-a-time `pc_match_len` is compiled
- No hardware multiply dependency in the hash function (uses addition-based multiply)
- All `memcpy`/`memmove` calls use standard library (compiler provides M0-safe versions)

## Encoder instrumentation

Compile with `-DPC_ENABLE_STATS` to collect per-encoder statistics with zero
overhead when disabled:

```c
#define PC_ENABLE_STATS
#include "picocompress.h"

pc_encoder enc;
pc_encoder_stats stats;

pc_encoder_init(&enc);
// ... encode data ...
pc_encoder_finish(&enc, write_fn, user);
pc_encoder_get_stats(&enc, &stats);

// stats.bytes_in, stats.bytes_out, stats.blocks
// stats.match_count, stats.repeat_hits, stats.dict_hits
// stats.lz_short_hits, stats.lz_long_hits
// stats.literal_bytes, stats.good_enough_hits, stats.lazy_improvements
```

**Important:** `PC_ENABLE_STATS` must be consistent across all translation units
that include `picocompress.h`, as it changes the `pc_encoder` struct layout.

## Deterministic output

Compressed output may vary depending on which acceleration paths are active
(e.g. CRC32 hash vs portable hash produces different bucket distributions).
The **decoder is always deterministic** — any build can decompress any stream.

For reproducible test output across platforms, force the portable paths:

```sh
gcc -DPC_NO_HW_CRC32 -DPC_NO_NEON -DPC_NO_MVE -DPC_NO_RVV -DPC_NO_BITSCAN ...
```
