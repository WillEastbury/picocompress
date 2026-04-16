# picocompress

Tiny dependency-free C compression library for embedded targets.

## Features

- Streaming encoder/decoder APIs (`pc_encoder_*`, `pc_decoder_*`)
- Buffer-based convenience APIs (`pc_compress_buffer`, `pc_decompress_buffer`)
- Small fixed memory footprint (no dynamic allocation)
- Tuned for short payloads (default block size: 508 bytes)

## Build test (MSVC)

```powershell
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress.c /Fe:test_picocompress.exe
.\test_picocompress.exe
```

## Additional tests

```powershell
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress_additional.c /Fe:test_picocompress_additional.exe
.\test_picocompress_additional.exe
```

## Benchmark harness

1. Install Python deps:

```powershell
python -m pip install brotli heatshrink2
```

2. Run the benchmark harness:

```powershell
.\run_benchmarks.ps1 -JsonOut benchmark_results.json
```

See `TEST_METHODS.md` and `PERFORMANCE_SUMMARY.md` for details/results.

## Public API

See `picocompress.h` for all constants, result codes, and function signatures.
