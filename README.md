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
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress.c /Fe:test_picocompress.exe
.\test_picocompress.exe

cl /nologo /O2 /W4 /TC picocompress.c test_picocompress_additional.c /Fe:test_picocompress_additional.exe
.\test_picocompress_additional.exe
```

Both suites verify compress→decompress roundtrips with **CRC32 checksums**.

## Benchmark harness

```powershell
python -m pip install brotli heatshrink2
.\run_benchmarks.ps1 -JsonOut benchmark_results.json
```

Benchmarks picocompress against heatshrink and brotli (q1/q5/default) across:
pattern, utf8+int, random, ASCII, JSON, lorem ipsum, RGB icon, JPEG,
pretty/minified JSON, UK addresses, order records, sparse, uint32 arrays,
and scaled sizes from 254 bytes to 1 MB.

See `TEST_METHODS.md` and `PERFORMANCE_SUMMARY.md` for methodology and results.

## Public API

See `picocompress.h` for all constants, result codes, and function signatures.
