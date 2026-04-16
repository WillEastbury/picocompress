# Performance summary

This summary is from the current benchmark harness run (`run_benchmarks.ps1`) on the local development machine.

## Memory window (current defaults)

- `PC_BLOCK_SIZE=508`
- `PC_HASH_BITS=9`
- `PC_HASH_CHAIN_DEPTH=2`
- Encode peak workspace: **3085 bytes**
- Decode state: **1028 bytes**

## Compression and speed results

### pattern-508 (508 bytes)

| Codec | Compressed bytes | Ratio | Saved bytes | Compress us | Decompress us |
|---|---:|---:|---:|---:|---:|
| picocompress | 111 | 4.577x | 397 | 10.28 | 22.86 |
| heatshrink(w11,l4) | 143 | 3.552x | 365 | 11.62 | 8.90 |
| brotli(q1,lgwin10) | 169 | 3.006x | 339 | 12.49 | 6.18 |
| brotli(q5,lgwin10) | 121 | 4.198x | 387 | 57.78 | 6.37 |
| brotli(default) | 119 | 4.269x | 389 | 4240.30 | 7.00 |

### utf8-int-508 (508 bytes)

| Codec | Compressed bytes | Ratio | Saved bytes | Compress us | Decompress us |
|---|---:|---:|---:|---:|---:|
| picocompress | 463 | 1.097x | 45 | 42.40 | 39.62 |
| heatshrink(w11,l4) | 460 | 1.104x | 48 | 168.28 | 22.07 |
| brotli(q1,lgwin10) | 391 | 1.299x | 117 | 25.35 | 7.90 |
| brotli(q5,lgwin10) | 334 | 1.521x | 174 | 93.43 | 9.13 |
| brotli(default) | 298 | 1.705x | 210 | 2634.34 | 11.77 |

### random-508 (508 bytes)

| Codec | Compressed bytes | Ratio | Saved bytes | Compress us | Decompress us |
|---|---:|---:|---:|---:|---:|
| picocompress | 512 | 0.992x | -4 | 117.41 | 40.94 |
| heatshrink(w11,l4) | 572 | 0.888x | -64 | 237.62 | 50.40 |
| brotli(q1,lgwin10) | 512 | 0.992x | -4 | 38.68 | 2.51 |
| brotli(q5,lgwin10) | 513 | 0.990x | -5 | 76.26 | 4.06 |
| brotli(default) | 512 | 0.992x | -4 | 20059.92 | 1.78 |

### ascii-254 (254 bytes)

| Codec | Compressed bytes | Ratio | Saved bytes | Compress us | Decompress us |
|---|---:|---:|---:|---:|---:|
| picocompress | 134 | 1.896x | 120 | 22.59 | 21.98 |
| heatshrink(w11,l4) | 156 | 1.628x | 98 | 21.06 | 13.81 |
| brotli(q1,lgwin10) | 127 | 2.000x | 127 | 11.00 | 5.00 |
| brotli(q5,lgwin10) | 104 | 2.442x | 150 | 40.94 | 5.02 |
| brotli(default) | 91 | 2.791x | 163 | 1011.58 | 4.91 |

## Raw results artifact

The machine-readable run output is saved as:

- `benchmark_results.json`
