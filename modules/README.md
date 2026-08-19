# Codec modules

`picocompress` is the host/shell. Compression formats live behind the versioned
codec ABI in `include/picocompress/codec.h`.

## Naming

Dynamic modules use conventional names so the host can discover them:

- Windows: `picocodec_<name>.dll`
- Linux: `libpicocodec_<name>.so`
- macOS: `libpicocodec_<name>.dylib`

Every module exports exactly one ABI query symbol:

```c
PCX_CODEC_EXPORT const pcx_codec_v1 *picocompress_codec_query(void);
```

The returned descriptor owns no host memory and must remain valid while the
module is loaded.

## Current modules

- `micro/` exposes the existing PicoCompress v3 format as codec name `micro`
  with aliases `picocompress` and `pc`. The original wire format and existing
  `src/picocompress.*` API remain byte-compatible.

PicoZstd, PicoBrotli, and future codecs should implement the same ABI in their
own repositories or modules. The shell must not gain codec-specific branches.

## Embedded targets

Targets without `dlopen` / `LoadLibrary` do not need a fake dynamic loader.
Link the codec normally and call `pcx_registry_register_static()` with the same
`pcx_codec_v1` descriptor. Dynamic and static paths intentionally share one
contract.
