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

## Public API

See `picocompress.h` for all constants, result codes, and function signatures.
