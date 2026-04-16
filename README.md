# picocompress

Tiny dependency-free C compression library for embedded targets.

## Features

- **Streaming** encoder/decoder APIs (`pc_encoder_*`, `pc_decoder_*`)
- **Buffer-based** convenience APIs (`pc_compress_buffer`, `pc_decompress_buffer`)
- **Small fixed memory**: ~3.1 KB encode, ~1.2 KB decode (no dynamic allocation)
- **Cross-block history**: 128-byte sliding window across blocks for improved multi-block ratio
- **64-entry static dictionary**: common JSON, CSV, HTTP, English, and binary patterns (ROM-resident)
- **Repeat-offset tokens**: 1-byte match for recurring struct strides
- **CRC32 roundtrip verification** in all test paths
- Tuned for short payloads (default block size: 508 bytes)

## Token format (v3)

| Range | Type | Size | Description |
|---|---|---|---|
| `0x00..0x3F` | Short literal | 1 + N | Run of 1–64 raw bytes |
| `0x40..0x7F` | Dictionary ref | 1 | Emit predefined sequence (64 entries) |
| `0x80..0xBF` | LZ match | 2 | 5-bit length + 9-bit offset |
| `0xC0..0xDF` | Repeat-offset | 1 | Reuse last LZ offset |
| `0xE0..0xFF` | Extended literal | 1 + N | Run of 65–96 raw bytes |

## Build and test (MSVC)

```powershell
cd src
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress.c /Fe:test_picocompress.exe
.\test_picocompress.exe

cl /nologo /O2 /W4 /TC picocompress.c test_picocompress_additional.c /Fe:test_picocompress_additional.exe
.\test_picocompress_additional.exe
```

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
# Balanced default (512 buckets × 2 deep, 504B history)
cl /O2 /TC src/picocompress.c ...

# Aggressive ratio (256 buckets × 4 deep — ~10% better, ~15% slower)
cl /O2 /TC /DPC_HASH_BITS=8u /DPC_HASH_CHAIN_DEPTH=4u src/picocompress.c ...

# Minimal RAM (256 buckets × 1 deep, 128B history)
cl /O2 /TC /DPC_HASH_BITS=8u /DPC_HASH_CHAIN_DEPTH=1u /DPC_HISTORY_SIZE=128u src/picocompress.c ...
```

| Config | Enc RAM | Dec RAM | Ratio | Speed |
|---|---:|---:|---|---|
| Default (512×2, h504) | ~4.6 KB | ~1.5 KB | Balanced | Balanced |
| Aggressive (256×4, h504) | ~4.6 KB | ~1.5 KB | Best | ~15% slower |
| Minimal (256×1, h128) | ~1.8 KB | ~0.7 KB | Lower | Fastest |

## Algorithm

See [`docs/ALGORITHM.md`](docs/ALGORITHM.md) for a detailed description of the compression
pipeline, token format, cross-block history, static dictionary, and performance tricks.
