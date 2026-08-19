#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$ROOT"

CC=${CC:-cc}
CFLAGS=${CFLAGS:-"-O2 -std=c11 -Wall -Wextra"}
DIST="$ROOT/dist"
mkdir -p "$DIST"

case "$(uname -s)" in
  Darwin)
    HOSTLIB="libpicocompress_host.dylib"
    CODECLIB="libpicocodec_micro.dylib"
    SHARED="-dynamiclib"
    DL_LIBS=""
    RPATH="-Wl,-rpath,@loader_path"
    ;;
  *)
    HOSTLIB="libpicocompress_host.so"
    CODECLIB="libpicocodec_micro.so"
    SHARED="-shared"
    DL_LIBS="-ldl"
    RPATH="-Wl,-rpath,\$ORIGIN"
    ;;
esac

# Host/registry shared library. It contains no codec implementation.
# shellcheck disable=SC2086
$CC $CFLAGS -fPIC -DPCX_HOST_BUILD -Iinclude $SHARED src/host.c -o "$DIST/$HOSTLIB" $DL_LIBS

# Native PicoCompress v3 codec as an independently loadable module.
# shellcheck disable=SC2086
$CC $CFLAGS -fPIC -Iinclude -Isrc $SHARED src/picocompress.c modules/micro/picocompress_micro.c -o "$DIST/$CODECLIB"

# Thin shell linked only to the host library, never to a codec.
# shellcheck disable=SC2086
$CC $CFLAGS -Iinclude src/cli.c -L"$DIST" -lpicocompress_host $RPATH -o "$DIST/picocompress" $DL_LIBS

# Registry unit test, using the static-registration fallback.
# shellcheck disable=SC2086
$CC $CFLAGS -DPCX_HOST_STATIC -Iinclude tests/test_modular_host.c src/host.c -o "$DIST/test_modular_host" $DL_LIBS
"$DIST/test_modular_host"

# Preserve the original native-codec regression suites and byte-stream contract.
# shellcheck disable=SC2086
$CC $CFLAGS -Isrc src/picocompress.c src/test_picocompress.c -o "$DIST/test_picocompress"
"$DIST/test_picocompress"
# shellcheck disable=SC2086
$CC $CFLAGS -Isrc src/picocompress.c src/test_picocompress_additional.c -o "$DIST/test_picocompress_additional"
"$DIST/test_picocompress_additional"

# Prove dynamic discovery and end-to-end shell round-trip.
printf '%s\n' 'picocompress modular codec roundtrip: alpha beta beta beta' > "$DIST/roundtrip.txt"
PICOCOMPRESS_CODEC_PATH="$DIST" "$DIST/picocompress" list | grep '^micro[[:space:]]'
PICOCOMPRESS_CODEC_PATH="$DIST" "$DIST/picocompress" compress micro "$DIST/roundtrip.txt" "$DIST/roundtrip.pc"
PICOCOMPRESS_CODEC_PATH="$DIST" "$DIST/picocompress" decompress micro "$DIST/roundtrip.pc" "$DIST/roundtrip.out"
cmp "$DIST/roundtrip.txt" "$DIST/roundtrip.out"

echo "picocompress modular build: ok"
