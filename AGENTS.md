# AGENTS.md

## Authority

Read `docs/MODULE_ABI.md` before changing host/module boundaries. Read
`docs/ALGORITHM.md` before changing the native micro codec format or algorithm.

## Architectural contract

- `picocompress` is a codec-neutral shell/registry, not a codec implementation.
- Codec-specific behavior belongs behind `pcx_codec_v1`.
- Do not add `if (codec == "...")` format logic to the host or CLI.
- Dynamic modules and statically linked embedded codecs must use the same ABI.
- The shell's canonical file path is streaming. Do not materialise whole input
  files merely because a codec has buffer convenience functions.
- Caller-owned state size/alignment is part of the ABI. Do not hide cross-DLL
  allocation ownership inside the v1 contract.
- Unknown codec options fail explicitly.

## Compatibility

The existing PicoCompress native format is the `micro` codec. Its v3 wire
format and cross-language byte identity are compatibility requirements.
Modularisation must not alter its tokens, static dictionary, profiles, or
streaming semantics.

Keep existing C and language-port regressions working when host/module code is
changed.

## Dependencies

The host may use the platform dynamic-loader API and the C runtime. Do not add a
package manager or general compression dependency to make module discovery
work. Codec modules own their own implementation dependencies, and PicoSuite
codecs should remain dependency-free unless their own specification says
otherwise.

## Build outputs

Desktop builds produce a thin shell, a host shared library, and independently
loadable codec libraries. Generated binaries belong under `dist/` and are not
source-controlled.

Embedded/bare-metal builds may omit dynamic loading and register codec
descriptors statically.
