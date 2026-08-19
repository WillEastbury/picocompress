# picocompress

`picocompress` is a tiny codec-neutral compression shell plus the original
PicoCompress **micro** codec.

The project is being split into independently linkable components:

```text
picocompress shell
        |
        v
picocompress host / registry
        |
        +-- picocodec_micro      existing PicoCompress v3 format
        +-- picocodec_zstd       external/future PicoZstd module
        +-- picocodec_brotli     external/future PicoBrotli module
        +-- ...                  any codec implementing ABI v1
```

Desktop/server builds discover shared codec libraries at runtime. Embedded and
bare-metal targets use the exact same codec descriptor through static
registration, so dynamic loading is optional rather than an architectural
requirement.

## The native micro codec

The original PicoCompress codec remains dependency-free C and keeps its existing
wire format, profiles, ports, and performance characteristics.

> **Decode at roughly 200-540 MB/s. Encode at roughly 20-47 MB/s. Using only a
> few KB of RAM depending on profile.**

It is still designed to run from Cortex-M0-class systems through Raspberry Pi
and general-purpose x86/AArch64 hosts.

Existing source users can continue to use:

```c
#include "picocompress.h"

pc_compress_buffer(...);
pc_decompress_buffer(...);
```

The modular build simply exposes that same implementation as codec name
`micro` (aliases: `picocompress`, `pc`). Modularisation must not change the v3
bitstream or cross-language byte identity.

## Modular shell

Build on Linux/macOS:

```sh
sh build.sh
```

Build on Windows from a Visual Studio developer environment:

```powershell
.\build.ps1
```

Desktop output is written to `dist/` and contains:

```text
picocompress                 shell executable
libpicocompress_host.*       codec registry / loader
libpicocodec_micro.*         native micro codec module
```

Windows uses the corresponding `.exe` / `.dll` names.

List installed codecs:

```text
picocompress list
```

Compress and decompress by codec name:

```text
picocompress compress micro input.txt output.pc
picocompress decompress micro output.pc restored.txt
```

Codec discovery searches `PICOCOMPRESS_CODEC_PATH`, then the current directory
and `./codecs`. Modules can also be supplied explicitly with `--plugin` or
`--codec-dir`.

The shell uses streaming entrypoints for file conversion. It never requires an
entire file to be materialised merely because a codec also exposes a buffer
helper.

## Codec ABI

See [`docs/MODULE_ABI.md`](docs/MODULE_ABI.md).

A dynamic codec exports one symbol:

```c
PCX_CODEC_EXPORT const pcx_codec_v1 *picocompress_codec_query(void);
```

The descriptor declares:

- codec name and aliases;
- compress/decompress capabilities;
- encoder/decoder state size and alignment;
- streaming init/sink/finish functions;
- optional buffer convenience functions.

The caller owns codec state memory. That avoids cross-DLL allocator ownership
and keeps the same contract usable on fixed-memory embedded targets.

## Embedded/static use

Targets without a dynamic loader register a linked descriptor directly:

```c
pcx_registry registry;
pcx_registry_init(&registry);
pcx_registry_register_static(&registry, codec);
```

No codec-specific behavior belongs in the host. PicoZstd, PicoBrotli, and future
PicoSuite codecs should be able to plug in without changing the shell.

## Micro codec profiles

The native codec scales from very small MCU configurations to larger embedded
profiles through compile-time settings. All profiles emit decoder-compatible
streams.

| Profile | Typical encoder RAM | Typical decoder RAM | Target |
|---|---:|---:|---|
| Micro | ~1.0 KB | ~0.5 KB | Cortex-M0 / tiny MCU |
| Minimal | ~1.8 KB | ~0.7 KB | small MCU |
| Balanced | ~4.6 KB | ~1.5 KB | Pico W / ESP32 |
| Q3 | ~7.7 KB | ~2.0 KB | Pico 2W / medium MCU |
| Q4 | ~13.8 KB | ~3.0 KB | Pi / Linux SBC |

The codec includes streaming APIs, cross-block history, a ROM static dictionary,
repeat-offset caching, and optional hardware acceleration paths for NEON, MVE,
RISC-V Vector, CRC32-assisted hashing, and CLZ/CTZ matching.

See:

- [`docs/ALGORITHM.md`](docs/ALGORITHM.md) for the v3 micro-codec format;
- [`docs/PLATFORM_SUPPORT.md`](docs/PLATFORM_SUPPORT.md) for acceleration paths;
- [`docs/PERFORMANCE_SUMMARY.md`](docs/PERFORMANCE_SUMMARY.md) for benchmarks;
- [`docs/PORTING_GUIDE.md`](docs/PORTING_GUIDE.md) for language/board ports;
- [`docs/MODULE_ABI.md`](docs/MODULE_ABI.md) for the modular host contract.

## Repository shape

```text
include/picocompress/        host/module ABI
src/host.c                   dynamic/static codec registry
src/cli.c                    thin codec-neutral shell
src/picocompress.c/.h        native v3 micro codec implementation
modules/micro/               micro codec ABI adapter
ports/                       existing language/board micro-codec ports
docs/                        algorithm, ABI, platform and benchmark docs
tests/                       host/module regressions
```

The important boundary is simple: **the shell selects codecs; codecs implement
compression.**
