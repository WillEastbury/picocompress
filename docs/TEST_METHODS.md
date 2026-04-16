# Test methods and harness

## 1. Correctness tests

### `test_picocompress.c`

Primary smoke and regression test:

- One-shot buffer compress/decompress roundtrip with **CRC32 verification**
- Streaming encoder/decoder roundtrip with variable chunk sizes and **CRC32 verification**
- Deterministic patterned input (`4096` bytes, exercises cross-block history)

Build/run:

```powershell
cd src
cl /nologo /O2 /W4 /TC picocompress.c test_picocompress.c /Fe:test_picocompress.exe
.\test_picocompress.exe
```

### `test_picocompress_additional.c`

Edge and robustness coverage:

- Zero-length input behavior
- Raw-block fallback on incompressible data with **CRC32 verification**
- Output-too-small error paths (compress and decompress)
- Corrupt stream detection
- Streaming roundtrip on a `508`-byte payload with **CRC32 verification**

Build/run:

```powershell
cd src
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

**Small/embedded payloads (254–508 bytes):**

- `pattern-508` — structured repeating bytes
- `utf8-int-508` — UTF-8 text + packed integer records
- `random-508` — deterministic pseudo-random (incompressible baseline)
- `ascii-254` — ASCII key-value telemetry text
- `json-508` — realistic JSON API response
- `lorem-508` — classic lorem ipsum prose
- `rgb-icon-508` — 13×13 uncompressed RGB pixel icon
- `jpeg-508` — synthetic JPEG-like high-entropy blob

**Structured data (4 KB):**

- `json-4K-pretty` — pretty-printed JSON with nested objects/arrays
- `json-4K-minified` — same logical content, minified (no whitespace)

**Scaled payloads (1 KB – 1 MB, six data types each):**

- `random` — pseudo-random bytes
- `sparse` — mostly zeros with occasional spikes
- `uint32` — packed little-endian counters with small deltas
- `utf8-prose` — repeating English-like log/event sentences
- `uk-addr` — UK names and addresses
- `order-pos` — fixed-width positional order records

### Metrics collected per codec per payload

- Compressed size (bytes) and ratio
- Compress time (microseconds/call)
- Decompress time (microseconds/call)
- Peak encode RAM (bytes, fixed per codec configuration)
- Peak decode RAM (bytes, fixed per codec configuration)

### Timing method

- Per-codec compress/decompress timings are measured in microseconds per call.
- Harness doubles iteration count until minimum wall-time threshold is hit.
- Roundtrip verification (exact byte match) is required for every codec/payload row.

## 3. Memory-window accounting

The project currently targets:

- Encode working set under ~`4 KB`
- Decode working set under ~`2 KB`

With current defaults (`PC_BLOCK_SIZE=508`, `PC_HASH_BITS=9`, `PC_HASH_CHAIN_DEPTH=2`, `PC_HISTORY_SIZE=128`):

- Encoder struct: `640` bytes (block + history)
- Encoder stack peak: ~`3200` bytes (hash table + combined buffer + output scratch)
- Decoder struct: ~`1160` bytes (payload + raw + history)
- 64-entry static dictionary: ~`350` bytes ROM/flash (zero RAM)
