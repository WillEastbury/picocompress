# Test methods and harness

## 1. Correctness tests

### `test_picocompress.c`

Primary smoke and regression test:

- One-shot buffer compress/decompress roundtrip
- Streaming encoder/decoder roundtrip with variable chunk sizes
- Deterministic patterned input (`4096` bytes)

Build/run:

```powershell
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress.c /Fe:test_picocompress.exe
.\test_picocompress.exe
```

### `test_picocompress_additional.c`

Edge and robustness coverage:

- Zero-length input behavior
- Raw-block fallback on incompressible data
- Output-too-small error paths (compress and decompress)
- Corrupt stream detection
- Streaming roundtrip on a `508`-byte payload

Build/run:

```powershell
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress_additional.c /Fe:test_picocompress_additional.exe
.\test_picocompress_additional.exe
```

## 2. Performance harness

### Files

- `benchmark_harness.py` — benchmark runner (picocompress, heatshrink, brotli)
- `run_benchmarks.ps1` — builds `picocompress.dll` with MSVC and runs the Python harness

### Dependencies

```powershell
python -m pip install brotli heatshrink2
```

### Run

```powershell
.\run_benchmarks.ps1 -JsonOut benchmark_results.json
```

### Payloads benchmarked

- `pattern-508` (structured repeating bytes, 508 bytes)
- `utf8-int-508` (typical UTF-8 text + packed integer records, 508 bytes)
- `random-508` (deterministic pseudo-random, 508 bytes)
- `ascii-254` (ASCII-only record-like text, 254 bytes)

### Timing method

- Per-codec compress/decompress timings are measured in microseconds per call.
- Harness doubles iteration count until minimum wall-time threshold is hit.
- Roundtrip verification is required for every codec/payload row.

## 3. Memory-window accounting

The project currently targets:

- Encode working set under ~`4 KB`
- Decode working set under ~`2 KB`

With current defaults (`PC_BLOCK_SIZE=508`, `PC_HASH_BITS=9`, `PC_HASH_CHAIN_DEPTH=2`):

- Encode peak workspace: `3085` bytes
- Decode state: `1028` bytes
