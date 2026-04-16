# picocompress

Tiny dependency-free C compression library for embedded targets.

## Features

- **Streaming** encoder/decoder APIs (`pc_encoder_*`, `pc_decoder_*`)
- **Buffer-based** convenience APIs (`pc_compress_buffer`, `pc_decompress_buffer`)
- **Small fixed memory**: ~3.1 KB encode, ~1.2 KB decode (no dynamic allocation)
- **Cross-block history**: 128-byte sliding window across blocks for improved multi-block ratio
- **64-entry static dictionary**: common JSON, CSV, HTTP, English, and binary patterns (ROM-resident)
- **Repeat-offset tokens**: 1-byte match for recurring struct strides
- **Hardware acceleration** via conditional compilation (NEON, MVE, RISC-V V, CRC32, CLZ/CTZ)
- **Encoder instrumentation** counters (`-DPC_ENABLE_STATS`)
- **CRC32 roundtrip verification** in all test paths
- Tuned for short payloads (default block size: 508 bytes)
- Cortex-M0 safe (no unaligned loads, pure portable fallback)

## Token format (v3)

| Range | Type | Size | Description |
|---|---|---|---|
| `0x00..0x3F` | Short literal | 1 + N | Run of 1–64 raw bytes |
| `0x40..0x7F` | Dictionary ref | 1 | Emit predefined sequence (64 entries) |
| `0x80..0xBF` | LZ match | 2 | 5-bit length + 9-bit offset |
| `0xC0..0xDF` | Repeat-offset | 1 | Reuse last LZ offset |
| `0xE0..0xEF` | Extended literal | 1 + N | Run of 65–80 raw bytes |
| `0xF0..0xFF` | Long-offset LZ | 3 | 4-bit length + 16-bit offset |

## Build and test (MSVC)

```powershell
cd src
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress.c /Fe:test_picocompress.exe
.\test_picocompress.exe

cl /nologo /O2 /W4 /TC picocompress.c test_picocompress_additional.c /Fe:test_picocompress_additional.exe
.\test_picocompress_additional.exe
```

## Build and test (GCC / Clang)

```sh
cd src
gcc -O2 -Wall -Wextra -std=c99 picocompress.c test_picocompress.c -o test_picocompress
./test_picocompress

gcc -O2 -Wall -Wextra -std=c99 picocompress.c test_picocompress_additional.c -o test_picocompress_additional
./test_picocompress_additional

# With encoder stats
gcc -O2 -Wall -Wextra -std=c99 -DPC_ENABLE_STATS picocompress.c test_picocompress_stats.c -o test_stats
./test_stats
```

Hardware acceleration is auto-detected. Override with `-DPC_NO_*` flags
(see [`docs/PLATFORM_SUPPORT.md`](docs/PLATFORM_SUPPORT.md)).

Both suites verify compress→decompress roundtrips with **CRC32 checksums**.

## Benchmark harness

```powershell
python -m pip install brotli heatshrink2
cd docs
.\run_benchmarks.ps1 -JsonOut benchmark_results.json
```

Benchmarks picocompress against heatshrink and brotli (q1/q5/default) across:
pattern, utf8+int, random, ASCII, JSON, lorem ipsum, RGB icon, JPEG,
pretty/minified JSON, UK addresses, order records, sparse, uint32 arrays,
and scaled sizes from 254 bytes to 1 MB.

See [`docs/TEST_METHODS.md`](docs/TEST_METHODS.md) and [`docs/PERFORMANCE_SUMMARY.md`](docs/PERFORMANCE_SUMMARY.md) for methodology and results.
Full data: [`docs/benchmark_results.json`](docs/benchmark_results.json) and [`docs/benchmark_tables.txt`](docs/benchmark_tables.txt).

## Test fixtures

The [`tests/`](tests/) directory contains all 64 deterministic payload files used by the benchmark harness (254 bytes to 1 MB).

## Public API

See [`src/picocompress.h`](src/picocompress.h) for all constants, result codes, and function signatures.

## Configuration

Hash and history parameters are **encoder-only** — the decoder never sees them.
Any encoder config produces streams that **any decoder build can decompress**.
Override at compile time with `-D`:

```powershell
# Micro (~1K encode, ~0.5K decode — fits in 2K SRAM total)
cl /O2 /TC /DPC_BLOCK_SIZE=192u /DPC_HASH_BITS=8u /DPC_HASH_CHAIN_DEPTH=1u /DPC_HISTORY_SIZE=64u src/picocompress.c ...

# Minimal (~2K encode, ~0.7K decode)
cl /O2 /TC /DPC_HASH_BITS=8u /DPC_HASH_CHAIN_DEPTH=1u /DPC_HISTORY_SIZE=128u src/picocompress.c ...

# Balanced default (~5K encode, ~1.5K decode)
cl /O2 /TC src/picocompress.c ...

# Aggressive ratio (~5K encode — deeper chain, same RAM)
cl /O2 /TC /DPC_HASH_BITS=8u /DPC_HASH_CHAIN_DEPTH=4u src/picocompress.c ...

# Super-aggressive 10K encode (Q3: wider hash + larger history + 2-step lazy)
cl /O2 /TC /DPC_HASH_BITS=10u /DPC_HASH_CHAIN_DEPTH=2u /DPC_HISTORY_SIZE=1024u /DPC_LAZY_STEPS=2u src/picocompress.c ...

# Ultra 15K encode (Q4: widest hash + deepest history + 2-step lazy)
cl /O2 /TC /DPC_HASH_BITS=11u /DPC_HASH_CHAIN_DEPTH=2u /DPC_HISTORY_SIZE=2048u /DPC_LAZY_STEPS=2u src/picocompress.c ...

# Minimal RAM (~2K encode)
cl /O2 /TC /DPC_HASH_BITS=8u /DPC_HASH_CHAIN_DEPTH=1u /DPC_HISTORY_SIZE=128u src/picocompress.c ...
```

| Profile | Flags | Enc RAM | Dec RAM | Ratio | Speed |
|---|---|---:|---:|---|---|
| Micro | `b192 b8 d1 h64` | ~1.0 KB | ~0.5 KB | ~60% of balanced | Fastest |
| Minimal | `b8 d1 h128` | ~1.8 KB | ~0.7 KB | ~75% of balanced | Fast |
| Balanced | (default) | ~4.6 KB | ~1.5 KB | Good | Good |
| Aggressive | `b8 d4` | ~4.6 KB | ~1.5 KB | +10% | −15% |
| Q3 | `b10 d2 h1024 lazy2` | ~7.7 KB | ~2.0 KB | +15% | −10% |
| Q4 | `b11 d2 h2048 lazy2` | ~13.8 KB | ~3.0 KB | +20% | −13% |

## Algorithm

See [`docs/ALGORITHM.md`](docs/ALGORITHM.md) for a detailed description of the compression
pipeline, token format, cross-block history, static dictionary, and performance tricks.

## Platform support & hardware acceleration

See [`docs/PLATFORM_SUPPORT.md`](docs/PLATFORM_SUPPORT.md) for hardware acceleration paths
(NEON, MVE, RISC-V V, CRC32, CLZ/CTZ), Cortex-M0 minimum profile, encoder
instrumentation counters, and override flags.
