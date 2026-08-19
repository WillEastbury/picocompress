# PicoCompress codec module ABI v1

The PicoCompress host is a codec-neutral registry and streaming shell. A codec
is a separately linkable component that implements `pcx_codec_v1` from
`include/picocompress/codec.h`.

## Design goals

- keep codec implementations independent from the shell;
- permit runtime discovery on Windows/Linux/macOS;
- permit static registration on embedded/bare-metal targets;
- preserve bounded streaming for arbitrarily large inputs;
- keep the ABI C-compatible and compiler-neutral;
- allow decoder-only codecs such as an initial PicoZstd implementation;
- make unsupported features explicit rather than silently changing behavior.

## Discovery

Desktop/server builds load modules by filename convention:

- `picocodec_*.dll`
- `libpicocodec_*.so`
- `libpicocodec_*.dylib`

Directories are supplied by `PICOCOMPRESS_CODEC_PATH`, `--codec-dir`, or the
shell defaults (`.` and `./codecs`). A single explicit module can be supplied
with `--plugin`.

The module must export:

```c
const pcx_codec_v1 *picocompress_codec_query(void);
```

The host validates `abi_version`, `struct_size`, the codec name, capabilities,
state sizes/alignment, and required streaming function pointers before adding
the codec to the registry.

## State ownership

The descriptor declares encoder/decoder state size and alignment. The caller
owns that memory and passes it to `*_init`, `*_sink`, and `*_finish`.

This deliberately avoids requiring a codec to expose allocation ownership
across a DLL boundary. A constrained target may place state in fixed memory; a
desktop shell may allocate it dynamically.

## Streaming contract

A codec advertising compression or decompression must also advertise
`PCX_CODEC_CAP_STREAMING` and provide the corresponding streaming functions.

`*_sink` accepts arbitrary input fragmentation. Callers are allowed to provide
one byte at a time, whole files in chunks, or any boundary in between. A codec
must not rely on shell chunk boundaries matching its own format boundaries.

Output is emitted through `pcx_write_fn`. A non-zero callback result is a write
failure and must stop the operation.

Optional buffer helpers exist for convenience, but the shell does not use them
for file conversion.

## Options

The v1 ABI uses key/value string options. Codecs must reject options they do not
understand with `PCX_ERR_UNSUPPORTED`; silently accepting unknown options is not
allowed.

This keeps codec-specific knobs such as Zstd level or Brotli quality outside the
host ABI while avoiding per-codec headers in the shell.

## Compatibility

`PCX_CODEC_ABI_V1` is immutable. Additive changes require a new descriptor
version rather than changing field meaning or layout in place.

The existing PicoCompress v3 micro-codec wire format is separately immutable.
Modularisation must not alter byte identity across the C, Python, JavaScript,
Go, Java, C#, Rust, or Arduino ports.

## Static registration

Embedded builds can omit `src/host.c` dynamic-loader branches entirely at link
time and register linked descriptors via:

```c
pcx_registry_register_static(&registry, codec);
```

A codec that works dynamically must therefore be usable statically without a
second adapter contract.
