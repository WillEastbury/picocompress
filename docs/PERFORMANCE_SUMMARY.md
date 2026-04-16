# Performance summary

Results from the benchmark harness ([`benchmark_harness.py`](benchmark_harness.py)) run via [`run_benchmarks.ps1`](run_benchmarks.ps1).

Machine-readable results: [`benchmark_results.json`](benchmark_results.json)
Markdown tables: [`benchmark_tables.txt`](benchmark_tables.txt)
Test methodology: [`TEST_METHODS.md`](TEST_METHODS.md)

All roundtrips are **CRC32-verified** (see [`../src/test_picocompress.c`](../src/test_picocompress.c) and [`../src/test_picocompress_additional.c`](../src/test_picocompress_additional.c)).

## Memory footprint (balanced default)

| Parameter | Value |
|---|---|
| `PC_BLOCK_SIZE` | 508 |
| `PC_HASH_BITS` | 9 |
| `PC_HASH_CHAIN_DEPTH` | 2 |
| `PC_HISTORY_SIZE` | 504 |
| `PC_DICT_COUNT` | 64 |
| `PC_LAZY_STEPS` | 1 |

| Codec | Encode RAM | Decode RAM |
|---|---:|---:|
| picocompress | **3.0 KB** | **1.0 KB** |
| heatshrink(w11,l4) | 12.5 KB | 2.0 KB |
| brotli(q1,lgwin10) | 16.7 KB | 31.5 KB |
| brotli(q5,lgwin10) | 553.6 KB | 31.5 KB |
| brotli(default) | 722.1 KB | 32.9 KB |

## Encoder profiles

| Profile | Flags | Enc RAM | Dec RAM | Best for |
|---|---|---:|---:|---|
| Micro | `b192 b8 d1 h64` | ~1.0 KB | ~0.5 KB | Cortex-M0, 2K SRAM |
| Minimal | `b8 d1 h128` | ~1.8 KB | ~0.7 KB | Tiny MCUs |
| Balanced | (default) | ~4.6 KB | ~1.5 KB | General embedded |
| Aggressive | `b8 d4` | ~4.6 KB | ~1.5 KB | Same RAM, +10% ratio |
| Q3 | `b10 d2 h1024 lazy2` | ~7.7 KB | ~2.0 KB | Medium MCU |
| Q4 | `b11 d2 h2048 lazy2` | ~13.8 KB | ~3.0 KB | Larger embedded |

All profiles produce **decoder-compatible** streams. Any encoder, any decoder.

## Encoder optimizations (v5)

1. **Repeat-offset cache** (3-entry): try recent offsets before hash lookup
2. **Length-2/3 fast path**: inline 2-byte compare before full match
3. **Token-first heuristic**: dictionary checked before LZ with first-byte filter
4. **Lazy only if short**: skip lazy when match >= 8 bytes
5. **Offset scoring**: tie-break by smaller offset; long-offset bonus when 2+ bytes longer
6. **Boundary-boost**: re-inject history tail into hash slot 0
7. **Literal run extension**: skip savings<=1 matches mid-run
8. **Hash function**: a*251 + b*11 + c*3 (tested 6 alternatives, current wins)
9. **Early reject**: check first byte before pc_match_len
10. **Good-enough threshold**: stop probing at 8+ byte match

## Results by payload
### pattern-508 (508 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 107 | 4.748x | 401 | 10.67 | 15.45 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 143 | 3.552x | 365 | 7.40 | 4.65 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 169 | 3.006x | 339 | 8.64 | 3.87 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 121 | 4.198x | 387 | 40.01 | 4.49 | 553.6K | 31.5K |
| brotli(default) | 119 | 4.269x | 389 | 1526.21 | 4.78 | 722.1K | 32.9K |
### utf8-int-508 (508 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 409 | 1.242x | 99 | 78.54 | 27.71 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 460 | 1.104x | 48 | 115.39 | 14.67 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 391 | 1.299x | 117 | 10.77 | 5.60 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 334 | 1.521x | 174 | 42.59 | 5.58 | 553.6K | 31.5K |
| brotli(default) | 298 | 1.705x | 210 | 1625.22 | 8.13 | 722.1K | 32.9K |
### random-508 (508 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 512 | 0.992x | -4 | 90.72 | 26.49 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 572 | 0.888x | -64 | 32.01 | 15.90 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 512 | 0.992x | -4 | 13.93 | 1.00 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 513 | 0.990x | -5 | 36.80 | 0.97 | 553.6K | 31.5K |
| brotli(default) | 512 | 0.992x | -4 | 2453.11 | 0.98 | 722.1K | 32.9K |
### ascii-254 (254 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 116 | 2.190x | 138 | 26.26 | 16.10 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 156 | 1.628x | 98 | 14.62 | 9.17 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 127 | 2.000x | 127 | 7.76 | 3.24 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 104 | 2.442x | 150 | 28.36 | 3.20 | 553.6K | 31.5K |
| brotli(default) | 91 | 2.791x | 163 | 823.21 | 3.32 | 722.1K | 32.9K |
### json-508 (508 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 264 | 1.924x | 244 | 55.91 | 27.30 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 312 | 1.628x | 196 | 23.18 | 12.81 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 303 | 1.677x | 205 | 10.25 | 5.22 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 248 | 2.048x | 260 | 38.93 | 5.04 | 553.6K | 31.5K |
| brotli(default) | 225 | 2.258x | 283 | 1573.30 | 6.35 | 722.1K | 32.9K |
### lorem-508 (508 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 444 | 1.144x | 64 | 88.73 | 27.75 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 448 | 1.134x | 60 | 33.59 | 15.18 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 317 | 1.603x | 191 | 10.40 | 5.20 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 308 | 1.649x | 200 | 41.55 | 5.30 | 553.6K | 31.5K |
| brotli(default) | 299 | 1.699x | 209 | 1206.49 | 6.74 | 722.1K | 32.9K |
### rgb-icon-508 (508 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 271 | 1.875x | 237 | 49.94 | 26.71 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 301 | 1.688x | 207 | 37.28 | 12.34 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 178 | 2.854x | 330 | 9.21 | 4.53 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 115 | 4.417x | 393 | 31.98 | 4.65 | 553.6K | 31.5K |
| brotli(default) | 111 | 4.577x | 397 | 1199.63 | 5.57 | 722.1K | 32.9K |
### jpeg-508 (508 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 512 | 0.992x | -4 | 90.43 | 26.74 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 571 | 0.890x | -63 | 76.41 | 15.93 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 512 | 0.992x | -4 | 14.36 | 1.03 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 513 | 0.990x | -5 | 39.82 | 1.02 | 553.6K | 31.5K |
| brotli(default) | 512 | 0.992x | -4 | 2604.44 | 1.02 | 722.1K | 32.9K |
### json-4K-pretty (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 988 | 4.146x | 3108 | 168.31 | 186.52 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 916 | 4.472x | 3180 | 129.61 | 34.50 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1233 | 3.322x | 2863 | 35.27 | 19.71 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 559 | 7.327x | 3537 | 79.55 | 9.37 | 553.6K | 31.5K |
| brotli(default) | 465 | 8.809x | 3631 | 7785.47 | 9.30 | 722.1K | 32.9K |
### json-4K-minified (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1020 | 4.016x | 3076 | 174.12 | 182.01 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1039 | 3.942x | 3057 | 118.38 | 37.77 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1252 | 3.272x | 2844 | 33.76 | 19.87 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 629 | 6.512x | 3467 | 77.73 | 10.17 | 553.6K | 31.5K |
| brotli(default) | 557 | 7.354x | 3539 | 6241.59 | 11.07 | 722.1K | 32.9K |
### random-1K (1024 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1036 | 0.988x | -12 | 177.04 | 48.45 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1152 | 0.889x | -128 | 74.07 | 26.26 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1028 | 0.996x | -4 | 18.80 | 1.06 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1029 | 0.995x | -5 | 51.27 | 1.10 | 553.6K | 31.5K |
| brotli(default) | 1028 | 0.996x | -4 | 3320.67 | 1.08 | 722.1K | 32.9K |
### sparse-1K (1024 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 234 | 4.376x | 790 | 44.69 | 51.05 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 231 | 4.433x | 793 | 287.88 | 12.68 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 254 | 4.031x | 770 | 11.13 | 6.50 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 172 | 5.953x | 852 | 46.99 | 5.79 | 553.6K | 31.5K |
| brotli(default) | 169 | 6.059x | 855 | 2784.48 | 6.11 | 722.1K | 32.9K |
### uint32-1K (1024 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 773 | 1.325x | 251 | 139.85 | 52.45 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 758 | 1.351x | 266 | 85.48 | 22.49 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 684 | 1.497x | 340 | 16.50 | 8.24 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 310 | 3.303x | 714 | 57.62 | 9.64 | 553.6K | 31.5K |
| brotli(default) | 316 | 3.241x | 708 | 1538.68 | 7.01 | 722.1K | 32.9K |
### utf8-prose-1K (1024 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 573 | 1.787x | 451 | 64.66 | 29.04 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 616 | 1.662x | 408 | 29.54 | 11.64 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 436 | 2.349x | 588 | 6.47 | 4.10 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 382 | 2.681x | 642 | 44.74 | 6.51 | 553.6K | 31.5K |
| brotli(default) | 343 | 2.985x | 681 | 2196.25 | 7.11 | 722.1K | 32.9K |
### uk-addr-1K (1024 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 637 | 1.608x | 387 | 117.51 | 50.15 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 603 | 1.698x | 421 | 43.01 | 19.54 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 530 | 1.932x | 494 | 13.12 | 6.70 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 485 | 2.111x | 539 | 53.64 | 6.78 | 553.6K | 31.5K |
| brotli(default) | 434 | 2.359x | 590 | 2008.67 | 9.17 | 722.1K | 32.9K |
### order-pos-1K (1024 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 556 | 1.842x | 468 | 105.70 | 51.91 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 530 | 1.932x | 494 | 57.14 | 19.12 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 456 | 2.246x | 568 | 13.38 | 6.99 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 384 | 2.667x | 640 | 55.94 | 6.96 | 553.6K | 31.5K |
| brotli(default) | 369 | 2.775x | 655 | 2215.06 | 8.20 | 722.1K | 32.9K |
### random-2K (2048 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 2068 | 0.990x | -20 | 417.39 | 95.86 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 2304 | 0.889x | -256 | 171.61 | 61.22 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 2055 | 0.997x | -7 | 33.37 | 1.39 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 2053 | 0.998x | -5 | 63.79 | 1.18 | 553.6K | 31.5K |
| brotli(default) | 2052 | 0.998x | -4 | 4834.40 | 1.29 | 722.1K | 32.9K |
### sparse-2K (2048 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 442 | 4.633x | 1606 | 75.73 | 95.35 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 437 | 4.686x | 1611 | 502.19 | 20.04 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 490 | 4.180x | 1558 | 19.95 | 12.38 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 289 | 7.087x | 1759 | 65.96 | 8.33 | 553.6K | 31.5K |
| brotli(default) | 281 | 7.288x | 1767 | 5620.91 | 10.73 | 722.1K | 32.9K |
### uint32-2K (2048 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1519 | 1.348x | 529 | 275.18 | 98.64 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1497 | 1.368x | 551 | 143.60 | 39.91 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1362 | 1.504x | 686 | 30.57 | 16.63 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 588 | 3.483x | 1460 | 82.40 | 15.97 | 553.6K | 31.5K |
| brotli(default) | 592 | 3.459x | 1456 | 2550.25 | 14.47 | 722.1K | 32.9K |
### utf8-prose-2K (2048 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1088 | 1.882x | 960 | 211.02 | 96.43 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 923 | 2.219x | 1125 | 115.62 | 28.45 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 937 | 2.186x | 1111 | 22.38 | 11.57 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 635 | 3.225x | 1413 | 68.52 | 6.05 | 553.6K | 31.5K |
| brotli(default) | 458 | 4.472x | 1590 | 2301.81 | 5.87 | 722.1K | 32.9K |
### uk-addr-2K (2048 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1216 | 1.684x | 832 | 125.31 | 56.13 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 924 | 2.216x | 1124 | 55.70 | 17.77 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1097 | 1.867x | 951 | 13.60 | 8.94 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 804 | 2.547x | 1244 | 36.23 | 6.50 | 553.6K | 31.5K |
| brotli(default) | 693 | 2.955x | 1355 | 2055.52 | 8.11 | 722.1K | 32.9K |
### order-pos-2K (2048 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1058 | 1.936x | 990 | 110.62 | 56.83 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 929 | 2.205x | 1119 | 106.67 | 18.02 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 938 | 2.183x | 1110 | 13.27 | 8.98 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 659 | 3.108x | 1389 | 37.55 | 6.36 | 553.6K | 31.5K |
| brotli(default) | 605 | 3.385x | 1443 | 2300.76 | 7.78 | 722.1K | 32.9K |
### random-4K (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 4132 | 0.991x | -36 | 370.26 | 106.41 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 4608 | 0.889x | -512 | 251.93 | 66.99 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 4109 | 0.997x | -13 | 34.85 | 0.96 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 4101 | 0.999x | -5 | 38.95 | 0.73 | 553.6K | 31.5K |
| brotli(default) | 4100 | 0.999x | -4 | 5091.03 | 0.71 | 722.1K | 32.9K |
### sparse-4K (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 812 | 5.044x | 3284 | 71.47 | 111.03 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 822 | 4.983x | 3274 | 678.44 | 18.81 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 918 | 4.462x | 3178 | 20.88 | 13.87 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 496 | 8.258x | 3600 | 44.40 | 8.19 | 553.6K | 31.5K |
| brotli(default) | 477 | 8.587x | 3619 | 6050.86 | 9.71 | 722.1K | 32.9K |
### uint32-4K (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 3014 | 1.359x | 1082 | 301.10 | 112.55 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 2976 | 1.376x | 1120 | 187.12 | 51.61 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 2744 | 1.493x | 1352 | 32.66 | 23.93 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1147 | 3.571x | 2949 | 58.96 | 15.48 | 553.6K | 31.5K |
| brotli(default) | 1146 | 3.574x | 2950 | 2255.70 | 16.96 | 722.1K | 32.9K |
### utf8-prose-4K (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1937 | 2.115x | 2159 | 224.82 | 110.45 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1195 | 3.428x | 2901 | 114.59 | 23.42 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1801 | 2.274x | 2295 | 22.07 | 15.01 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1122 | 3.651x | 2974 | 47.62 | 8.88 | 553.6K | 31.5K |
| brotli(default) | 502 | 8.159x | 3594 | 4474.59 | 6.45 | 722.1K | 32.9K |
### uk-addr-4K (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 2339 | 1.751x | 1757 | 241.88 | 110.51 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1471 | 2.785x | 2625 | 132.91 | 28.26 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 2232 | 1.835x | 1864 | 25.55 | 17.29 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1450 | 2.825x | 2646 | 52.39 | 10.14 | 553.6K | 31.5K |
| brotli(default) | 1122 | 3.651x | 2974 | 3817.69 | 11.49 | 722.1K | 32.9K |
### order-pos-4K (4096 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 2066 | 1.983x | 2030 | 217.91 | 110.88 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1644 | 2.491x | 2452 | 284.98 | 31.53 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1895 | 2.161x | 2201 | 24.96 | 17.31 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1225 | 3.344x | 2871 | 63.00 | 10.66 | 553.6K | 31.5K |
| brotli(default) | 1050 | 3.901x | 3046 | 4780.17 | 13.29 | 722.1K | 32.9K |
### random-8K (8192 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 8260 | 0.992x | -68 | 783.15 | 211.28 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 9215 | 0.889x | -1023 | 515.91 | 131.97 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 8217 | 0.997x | -25 | 85.87 | 1.35 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 8197 | 0.999x | -5 | 59.19 | 0.86 | 553.6K | 31.5K |
| brotli(default) | 8196 | 1.000x | -4 | 12454.96 | 0.83 | 722.1K | 32.9K |
### sparse-8K (8192 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1691 | 4.844x | 6501 | 151.84 | 220.46 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1650 | 4.965x | 6542 | 1365.48 | 36.19 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1848 | 4.433x | 6344 | 45.42 | 27.44 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 980 | 8.359x | 7212 | 91.04 | 14.18 | 553.6K | 31.5K |
| brotli(default) | 925 | 8.856x | 7267 | 11697.77 | 20.96 | 722.1K | 32.9K |
### uint32-8K (8192 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 6179 | 1.326x | 2013 | 620.12 | 231.13 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 6011 | 1.363x | 2181 | 380.58 | 101.94 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 5539 | 1.479x | 2653 | 74.38 | 48.70 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 2289 | 3.579x | 5903 | 106.78 | 28.98 | 553.6K | 31.5K |
| brotli(default) | 2288 | 3.580x | 5904 | 4288.65 | 31.18 | 722.1K | 32.9K |
### utf8-prose-8K (8192 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 3674 | 2.230x | 4518 | 433.54 | 219.65 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1890 | 4.334x | 6302 | 222.13 | 38.05 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 3515 | 2.331x | 4677 | 45.10 | 29.35 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1838 | 4.457x | 6354 | 77.88 | 13.82 | 553.6K | 31.5K |
| brotli(default) | 582 | 14.076x | 7610 | 9984.65 | 7.16 | 722.1K | 32.9K |
### uk-addr-8K (8192 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 4496 | 1.822x | 3696 | 472.66 | 219.36 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 2494 | 3.285x | 5698 | 271.45 | 50.78 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 4495 | 1.822x | 3697 | 59.41 | 34.39 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 2670 | 3.068x | 5522 | 108.35 | 17.20 | 553.6K | 31.5K |
| brotli(default) | 1855 | 4.416x | 6337 | 7425.93 | 16.13 | 722.1K | 32.9K |
### order-pos-8K (8192 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 4089 | 2.003x | 4103 | 435.01 | 222.51 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 3092 | 2.649x | 5100 | 628.60 | 60.90 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 3791 | 2.161x | 4401 | 61.74 | 33.90 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 2332 | 3.513x | 5860 | 141.95 | 16.85 | 553.6K | 31.5K |
| brotli(default) | 1849 | 4.431x | 6343 | 8441.33 | 21.10 | 722.1K | 32.9K |
### random-16K (16384 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 16516 | 0.992x | -132 | 1474.68 | 420.09 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 18430 | 0.889x | -2046 | 1032.23 | 263.25 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 16433 | 0.997x | -49 | 189.28 | 2.42 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 16389 | 1.000x | -5 | 95.64 | 1.46 | 553.6K | 31.5K |
| brotli(default) | 16388 | 1.000x | -4 | 32059.85 | 1.56 | 722.1K | 32.9K |
### sparse-16K (16384 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 3491 | 4.693x | 12893 | 322.59 | 438.45 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 3299 | 4.966x | 13085 | 2722.95 | 73.75 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 3747 | 4.373x | 12637 | 127.35 | 56.36 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1951 | 8.398x | 14433 | 191.24 | 24.98 | 553.6K | 31.5K |
| brotli(default) | 1820 | 9.002x | 14564 | 24213.82 | 35.82 | 722.1K | 32.9K |
### uint32-16K (16384 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 12294 | 1.333x | 4090 | 1233.71 | 446.73 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 12018 | 1.363x | 4366 | 760.82 | 206.15 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 11096 | 1.477x | 5288 | 181.34 | 98.04 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 4545 | 3.605x | 11839 | 196.68 | 55.68 | 553.6K | 31.5K |
| brotli(default) | 4545 | 3.605x | 11839 | 9840.36 | 55.61 | 722.1K | 32.9K |
### utf8-prose-16K (16384 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 7169 | 2.285x | 9215 | 846.36 | 435.05 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 3609 | 4.540x | 12775 | 513.31 | 127.14 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 7096 | 2.309x | 9288 | 182.91 | 90.60 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 3282 | 4.992x | 13102 | 271.38 | 41.11 | 553.6K | 31.5K |
| brotli(default) | 721 | 22.724x | 15663 | 40237.83 | 14.65 | 722.1K | 32.9K |
### uk-addr-16K (16384 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 8803 | 1.861x | 7581 | 1563.67 | 737.22 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 4677 | 3.503x | 11707 | 763.02 | 160.19 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 8968 | 1.827x | 7416 | 232.72 | 109.47 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 5172 | 3.168x | 11212 | 428.64 | 53.84 | 553.6K | 31.5K |
| brotli(default) | 3242 | 5.054x | 13142 | 26153.33 | 36.41 | 722.1K | 32.9K |
### order-pos-16K (16384 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 7976 | 2.054x | 8408 | 1443.08 | 718.79 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 5976 | 2.742x | 10408 | 1682.50 | 203.07 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 7509 | 2.182x | 8875 | 226.22 | 124.73 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 4468 | 3.667x | 11916 | 464.25 | 60.89 | 553.6K | 31.5K |
| brotli(default) | 3285 | 4.988x | 13099 | 27334.49 | 51.18 | 722.1K | 32.9K |
### random-32K (32768 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 33028 | 0.992x | -260 | 2970.36 | 842.40 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 36860 | 0.889x | -4092 | 2076.06 | 526.71 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 32865 | 0.997x | -97 | 392.33 | 4.49 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 32773 | 1.000x | -5 | 48.17 | 2.54 | 553.6K | 31.5K |
| brotli(default) | 32772 | 1.000x | -4 | 6240.54 | 2.85 | 722.1K | 32.9K |
### sparse-32K (32768 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 7218 | 4.540x | 25550 | 688.88 | 906.20 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 6653 | 4.925x | 26115 | 5748.17 | 264.46 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 7654 | 4.281x | 25114 | 404.48 | 211.03 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 3961 | 8.273x | 28807 | 620.72 | 97.58 | 553.6K | 31.5K |
| brotli(default) | 3667 | 8.936x | 29101 | 49944.05 | 59.92 | 722.1K | 32.9K |
### uint32-32K (32768 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 24523 | 1.336x | 8245 | 2465.30 | 893.18 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 24066 | 1.362x | 8702 | 1572.90 | 405.49 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 22194 | 1.476x | 10574 | 393.90 | 226.09 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 9057 | 3.618x | 23711 | 380.69 | 112.65 | 553.6K | 31.5K |
| brotli(default) | 9061 | 3.616x | 23707 | 23066.51 | 117.27 | 722.1K | 32.9K |
### utf8-prose-32K (32768 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 13814 | 2.372x | 18954 | 1638.50 | 869.26 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 6534 | 5.015x | 26234 | 970.93 | 137.28 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 14368 | 2.281x | 18400 | 247.18 | 123.34 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 6075 | 5.394x | 26693 | 238.11 | 42.52 | 553.6K | 31.5K |
| brotli(default) | 971 | 33.747x | 31797 | 51785.91 | 10.72 | 722.1K | 32.9K |
### uk-addr-32K (32768 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 17468 | 1.876x | 15300 | 1828.63 | 898.62 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 8994 | 3.643x | 23774 | 1149.36 | 198.50 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 17912 | 1.829x | 14856 | 316.91 | 151.52 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 10073 | 3.253x | 22695 | 432.31 | 75.29 | 553.6K | 31.5K |
| brotli(default) | 5742 | 5.707x | 27026 | 31439.70 | 42.39 | 722.1K | 32.9K |
### order-pos-32K (32768 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 15975 | 2.051x | 16793 | 1719.76 | 890.29 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 11719 | 2.796x | 21049 | 2699.06 | 245.69 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 15026 | 2.181x | 17742 | 309.40 | 162.93 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 8939 | 3.666x | 23829 | 507.59 | 79.89 | 553.6K | 31.5K |
| brotli(default) | 6126 | 5.349x | 26642 | 35173.36 | 61.61 | 722.1K | 32.9K |
### random-128K (131072 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 132108 | 0.992x | -1036 | 11827.08 | 3480.93 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 147436 | 0.889x | -16364 | 8280.63 | 2092.52 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 131457 | 0.997x | -385 | 1616.27 | 14.99 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 131077 | 1.000x | -5 | 285.16 | 7.47 | 553.6K | 31.5K |
| brotli(default) | 131077 | 1.000x | -5 | 25486.57 | 8.67 | 722.1K | 32.9K |
### sparse-128K (131072 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 28141 | 4.658x | 102931 | 2643.21 | 3610.15 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 26236 | 4.996x | 104836 | 21700.00 | 583.67 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 30090 | 4.356x | 100982 | 1172.79 | 598.32 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 15321 | 8.555x | 115751 | 1506.34 | 260.74 | 553.6K | 31.5K |
| brotli(default) | 13926 | 9.412x | 117146 | 217348.90 | 217.04 | 722.1K | 32.9K |
### uint32-128K (131072 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 97726 | 1.341x | 33346 | 9982.08 | 3723.83 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 96152 | 1.363x | 34920 | 6420.41 | 1642.73 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 88745 | 1.477x | 42327 | 1668.91 | 1003.17 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 36075 | 3.633x | 94997 | 1438.93 | 429.65 | 553.6K | 31.5K |
| brotli(default) | 36072 | 3.634x | 95000 | 92607.80 | 452.17 | 722.1K | 32.9K |
### utf8-prose-128K (131072 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 53632 | 2.444x | 77440 | 6347.53 | 3562.41 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 24488 | 5.352x | 106584 | 3828.56 | 517.72 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 57603 | 2.275x | 73469 | 1064.95 | 529.25 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 22042 | 5.946x | 109030 | 1015.51 | 220.16 | 553.6K | 31.5K |
| brotli(default) | 2350 | 55.775x | 128722 | 226460.45 | 22.38 | 722.1K | 32.9K |
### uk-addr-128K (131072 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 69775 | 1.878x | 61297 | 7301.33 | 3707.38 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 35266 | 3.717x | 95806 | 4685.67 | 802.02 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 71651 | 1.829x | 59421 | 1335.38 | 663.01 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 39708 | 3.301x | 91364 | 1832.09 | 385.32 | 553.6K | 31.5K |
| brotli(default) | 19579 | 6.695x | 111493 | 131760.85 | 139.07 | 722.1K | 32.9K |
### order-pos-128K (131072 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 63727 | 2.057x | 67345 | 6660.85 | 3676.92 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 46146 | 2.840x | 84926 | 10519.60 | 980.77 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 60186 | 2.178x | 70886 | 1305.82 | 726.13 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 35101 | 3.734x | 95971 | 1863.06 | 415.35 | 553.6K | 31.5K |
| brotli(default) | 22222 | 5.898x | 108850 | 152444.25 | 266.01 | 722.1K | 32.9K |
### random-512K (524288 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 528420 | 0.992x | -4132 | 47669.63 | 14051.52 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 589735 | 0.889x | -65447 | 33214.16 | 8529.36 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 525825 | 0.997x | -1537 | 6490.61 | 81.63 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 524302 | 1.000x | -14 | 925.62 | 39.67 | 553.6K | 31.5K |
| brotli(default) | 524293 | 1.000x | -5 | 131553.90 | 72.95 | 722.1K | 32.9K |
### sparse-512K (524288 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 112131 | 4.676x | 412157 | 11056.42 | 14380.56 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 105190 | 4.984x | 419098 | 87664.27 | 2658.81 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 120470 | 4.352x | 403818 | 4752.42 | 2638.28 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 61606 | 8.510x | 462682 | 5956.87 | 1102.45 | 553.6K | 31.5K |
| brotli(default) | 54911 | 9.548x | 469377 | 886783.90 | 1037.11 | 722.1K | 32.9K |
### uint32-512K (524288 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 391434 | 1.339x | 132854 | 39027.59 | 14502.29 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 384608 | 1.363x | 139680 | 25326.76 | 6876.96 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 354787 | 1.478x | 169501 | 6563.06 | 4016.30 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 144325 | 3.633x | 379963 | 5762.17 | 1728.56 | 553.6K | 31.5K |
| brotli(default) | 144198 | 3.636x | 380090 | 551250.00 | 1837.45 | 722.1K | 32.9K |
### utf8-prose-512K (524288 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 210008 | 2.497x | 314280 | 24980.15 | 14291.37 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 99563 | 5.266x | 424725 | 15742.05 | 2110.86 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 227992 | 2.300x | 296296 | 4327.93 | 2215.05 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 85897 | 6.104x | 438391 | 3974.68 | 1043.09 | 553.6K | 31.5K |
| brotli(default) | 7916 | 66.231x | 516372 | 1002597.90 | 111.34 | 722.1K | 32.9K |
### uk-addr-512K (524288 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 278522 | 1.882x | 245766 | 29143.68 | 14807.47 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 139778 | 3.751x | 384510 | 18851.13 | 3236.33 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 286087 | 1.833x | 238201 | 5406.77 | 2753.48 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 158960 | 3.298x | 365328 | 6758.40 | 1630.36 | 553.6K | 31.5K |
| brotli(default) | 70570 | 7.429x | 453718 | 592131.20 | 526.52 | 722.1K | 32.9K |
### order-pos-512K (524288 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 254223 | 2.062x | 270065 | 27172.86 | 14769.99 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 184422 | 2.843x | 339866 | 43273.81 | 3970.04 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 240755 | 2.178x | 283533 | 5253.31 | 3011.55 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 140710 | 3.726x | 383578 | 7316.51 | 1737.30 | 553.6K | 31.5K |
| brotli(default) | 84586 | 6.198x | 439702 | 685833.40 | 1013.66 | 722.1K | 32.9K |
### random-1024K (1048576 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 1056836 | 0.992x | -8260 | 95276.55 | 27910.69 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 1179467 | 0.889x | -130891 | 68203.98 | 17789.37 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 1051649 | 0.997x | -3073 | 13503.10 | 417.44 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 1048602 | 1.000x | -26 | 1746.17 | 352.39 | 553.6K | 31.5K |
| brotli(default) | 1048581 | 1.000x | -5 | 343338.00 | 672.23 | 722.1K | 32.9K |
### sparse-1024K (1048576 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 222400 | 4.715x | 826176 | 21656.71 | 29264.32 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 209689 | 5.001x | 838887 | 173885.75 | 5851.89 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 239379 | 4.380x | 809197 | 9473.92 | 5174.80 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 122241 | 8.578x | 926335 | 11798.11 | 2491.30 | 553.6K | 31.5K |
| brotli(default) | 108687 | 9.648x | 939889 | 1936088.90 | 2119.20 | 722.1K | 32.9K |
### uint32-1024K (1048576 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 784899 | 1.336x | 263677 | 80941.27 | 29354.58 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 769755 | 1.362x | 278821 | 50988.20 | 13714.48 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 710117 | 1.477x | 338459 | 13227.00 | 8275.15 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 288959 | 3.629x | 759617 | 12122.16 | 3825.62 | 553.6K | 31.5K |
| brotli(default) | 288536 | 3.634x | 760040 | 819576.70 | 4163.98 | 722.1K | 32.9K |
### utf8-prose-1024K (1048576 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 419722 | 2.498x | 628854 | 50110.10 | 28873.20 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 197909 | 5.298x | 850667 | 31578.84 | 4912.50 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 455358 | 2.303x | 593218 | 8617.96 | 4678.91 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 157526 | 6.657x | 891050 | 8083.73 | 2462.20 | 553.6K | 31.5K |
| brotli(default) | 15333 | 68.387x | 1033243 | 1959459.40 | 656.13 | 722.1K | 32.9K |
### uk-addr-1024K (1048576 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 556947 | 1.883x | 491629 | 58602.22 | 29785.06 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 279839 | 3.747x | 768737 | 38643.64 | 7032.38 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 571618 | 1.834x | 476958 | 10813.53 | 5731.49 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 285137 | 3.677x | 763439 | 14806.35 | 3844.38 | 553.6K | 31.5K |
| brotli(default) | 136568 | 7.678x | 912008 | 1219987.30 | 1423.96 | 722.1K | 32.9K |
### order-pos-1024K (1048576 bytes)
| Codec | Compressed | Ratio | Saved | Compress us | Decompress us | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| picocompress | 509513 | 2.058x | 539063 | 54839.95 | 29685.40 | 3.0K | 1.0K |
| heatshrink(w11,l4) | 368971 | 2.842x | 679605 | 86825.05 | 8492.59 | 12.5K | 2.0K |
| brotli(q1,lgwin10) | 481828 | 2.176x | 566748 | 10470.56 | 6236.76 | 16.7K | 31.5K |
| brotli(q5,lgwin10) | 252930 | 4.146x | 795646 | 16054.64 | 3913.14 | 553.6K | 31.5K |
| brotli(default) | 166582 | 6.295x | 881994 | 1398984.90 | 2426.47 | 722.1K | 32.9K |

