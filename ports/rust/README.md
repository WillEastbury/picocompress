# picocompress (Rust port)

Pure Rust, `#![no_std]`-compatible port of the [picocompress](../../README.md)
block-based LZ compressor. Produces **byte-identical** compressed output to the
C reference implementation.

## Features

- Zero external dependencies
- `#![no_std]` compatible (requires `alloc`)
- All safe Rust — no `unsafe` blocks
- Static 96-entry dictionary identical to the C version

## Usage

Add to your `Cargo.toml`:

```toml
[dependencies]
picocompress = { path = "path/to/picocompress/ports/rust" }
```

### Compress and decompress

```rust
use picocompress::{compress, decompress};

let data = b"hello world hello world hello world";
let compressed = compress(data);
let original = decompress(&compressed).unwrap();
assert_eq!(&original, data);
```

### Custom options (builder pattern)

```rust
use picocompress::{compress_with, decompress, Options};

let opts = Options::new()
    .block_size(256)
    .history_size(128)
    .hash_bits(8)
    .hash_chain_depth(1)
    .lazy_steps(2);

let data = b"some data to compress with custom settings";
let compressed = compress_with(data, &opts);
let original = decompress(&compressed).unwrap();
assert_eq!(&original[..], &data[..]);
```

### Error handling

```rust
use picocompress::{decompress, Error};

match decompress(&[0xFF, 0xFF, 0x01, 0x00]) {
    Ok(data) => println!("decompressed {} bytes", data.len()),
    Err(Error::Corrupt) => eprintln!("data is corrupt"),
    Err(Error::InvalidInput) => eprintln!("invalid input"),
}
```

## API

| Function | Description |
|---|---|
| `compress(input: &[u8]) -> Vec<u8>` | Compress with default options |
| `compress_with(input: &[u8], opts: &Options) -> Vec<u8>` | Compress with custom options |
| `decompress(compressed: &[u8]) -> Result<Vec<u8>, Error>` | Decompress data |

## Token format

Matches the C implementation exactly:

| Byte range | Type | Size | Description |
|---|---|---|---|
| `0x00..0x3F` | Short literal | 1+N | Emit N raw bytes (N = 1..64) |
| `0x40..0x7F` | Dictionary ref | 1 | Entry 0..63 |
| `0x80..0xBF` | LZ match | 2 | 5-bit len + 9-bit offset |
| `0xC0..0xCF` | Repeat-offset | 1 | 4-bit len, reuse last offset |
| `0xD0..0xDF` | Dictionary ref | 1 | Entry 80..95 |
| `0xE0..0xEF` | Dictionary ref | 1 | Entry 64..79 |
| `0xF0..0xFF` | Long-offset LZ | 3 | 4-bit len + 16-bit offset BE |
