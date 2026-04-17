# picocompress — Python port

Pure-Python 3.10+ implementation of the [picocompress](../../README.md) compression library.
Zero external dependencies. Produces byte-identical compressed output to the C reference implementation (same dictionary, same token format, same block framing).

## Installation

Copy `picocompress.py` into your project — it's a single file with no dependencies.

```bash
cp picocompress.py /path/to/your/project/
```

## Quick start

### Buffer API

```python
import picocompress

# Compress
data = b'{"name":"sensor","value":42,"status":"active","type":"device"}'
compressed = picocompress.compress(data)
print(f"{len(data)} -> {len(compressed)} bytes")

# Decompress
original = picocompress.decompress(compressed)
assert original == data
```

### Streaming API

```python
import picocompress

# Encoder — feed data in chunks, get compressed blocks via callback
encoder = picocompress.Encoder()
output = bytearray()
encoder.sink(b'{"name":"test",', output.extend)
encoder.sink(b'"value":"hello"}', output.extend)
encoder.finish(output.extend)

# Decoder — feed compressed data in chunks
decoder = picocompress.Decoder()
result = bytearray()
decoder.sink(bytes(output), result.extend)
decoder.finish()

assert bytes(result) == b'{"name":"test","value":"hello"}'
```

### Profiles

```python
import picocompress

data = b"sensor data " * 500

# Use a named profile
compressed = picocompress.compress(data, profile=picocompress.PROFILES["q3"])

# Or define a custom profile
custom = picocompress.Profile(block_size=508, hash_bits=10, chain_depth=3,
                              history_size=1024, lazy_steps=2)
compressed = picocompress.compress(data, profile=custom)

# Any profile's output can be decompressed with the default decoder
original = picocompress.decompress(compressed)
```

### Available profiles

| Profile    | Block | Hash     | Depth | History | Lazy |
|------------|------:|----------|------:|--------:|-----:|
| micro      |   192 | 256 × 1 |     1 |      64 |    1 |
| minimal    |   508 | 256 × 1 |     1 |     128 |    1 |
| balanced   |   508 | 512 × 2 |     2 |     504 |    1 |
| aggressive |   508 | 256 × 4 |     4 |     504 |    1 |
| q3         |   508 | 1024 × 2|     2 |    1024 |    2 |
| q4         |   508 | 2048 × 2|     2 |    2048 |    2 |

The **balanced** profile is the default — it matches the C library's default configuration.

## API reference

### `compress(data, *, profile=None) -> bytes`
Compress `data` (bytes/bytearray) and return the compressed bytes.

### `decompress(data) -> bytes`
Decompress `data` and return the original bytes. Raises `CorruptDataError` on invalid input.

### `compress_bound(input_len) -> int`
Return the worst-case compressed size (input size + 4 bytes per block header).

### `Encoder(*, profile=None)`
Streaming encoder. Call `.sink(data, write_fn)` to feed data, `.finish(write_fn)` to flush.

### `Decoder()`
Streaming decoder. Call `.sink(data, write_fn)` to feed compressed data, `.finish()` to verify completion.

### `Profile(block_size, hash_bits, chain_depth, history_size, lazy_steps)`
Frozen dataclass for encoder configuration. The decoder is profile-independent.

## Error handling

```python
try:
    picocompress.decompress(b"corrupted data")
except picocompress.CorruptDataError as e:
    print(f"Bad data: {e}")
except picocompress.PicocompressError as e:
    print(f"Error: {e}")
```

## Running tests

```bash
python -m pytest test_picocompress.py -v
```

## Compatibility

- **Python**: 3.10+ (uses `X | Y` union type syntax)
- **Dependencies**: None
- **Output format**: Byte-identical to the C implementation with the portable hash function (`a*251 + b*11 + c*3`)
- **Decoder**: Can decompress data from any encoder profile or the C implementation
