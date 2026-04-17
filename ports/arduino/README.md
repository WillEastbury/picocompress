# Picocompress — Arduino Library

Tiny, zero-allocation LZ compression for microcontrollers.

## Installation

### Arduino IDE (manual)

1. Copy this `arduino/` folder (or download a ZIP) into your Arduino `libraries/` directory and rename it to `Picocompress`.
2. Restart the Arduino IDE.  The library appears under **Sketch → Include Library → Picocompress**.

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
    picocompress/Picocompress@^1.0.0
```

Or symlink / copy this directory into your project's `lib/` folder.

## Quick Start

```cpp
#include <Picocompress.h>

Picocompress pc;

uint8_t compressed[256];
size_t  compLen;
int rc = pc.compress(input, inputLen, compressed, sizeof(compressed), &compLen);
```

## API

### Constructor

```cpp
Picocompress(uint16_t blockSize = 508);
```

### Profile Presets

| Preset                    | Block Size | Target               |
|---------------------------|------------|----------------------|
| `Picocompress::Micro()`   | 252        | Arduino Uno (AVR)    |
| `Picocompress::Balanced()`| 508        | ESP32, Pico W, STM32 |
| `Picocompress::Q4()`      | 508        | Same as Balanced     |

### Methods

```cpp
int compress(const uint8_t *input, size_t inputLen,
             uint8_t *output, size_t outputCap, size_t *outputLen);

int decompress(const uint8_t *input, size_t inputLen,
               uint8_t *output, size_t outputCap, size_t *outputLen);

static size_t compressBound(size_t inputLen);

uint16_t blockSize() const;
```

### Return Codes

| Code                     | Value | Meaning                    |
|--------------------------|-------|----------------------------|
| `PC_OK`                  |  0    | Success                    |
| `PC_ERR_WRITE`           | -1    | Write callback failed      |
| `PC_ERR_INPUT`           | -2    | Invalid input              |
| `PC_ERR_CORRUPT`         | -3    | Corrupt compressed data    |
| `PC_ERR_OUTPUT_TOO_SMALL`| -4    | Output buffer too small    |

### Streaming API

For advanced use, the C-level streaming encoder/decoder is available directly:

```cpp
extern "C" {
#include "picocompress.h"
}

pc_encoder enc;
pc_encoder_init(&enc);
pc_encoder_sink(&enc, data, len, myWriteCallback, NULL);
pc_encoder_finish(&enc, myWriteCallback, NULL);
```

See `examples/StreamingCompress/` for a complete example.

## Examples

- **BasicCompress** — Compress and decompress a string, print size and timing.
- **StreamingCompress** — Feed data in chunks using the streaming encoder.

## Board Compatibility

Tested / designed for:

- **Arduino Uno** (ATmega328P) — use `Picocompress::Micro()`
- **ESP32 / ESP32-S3**
- **Raspberry Pi Pico W** (RP2040)
- Any board with a C99 compiler and ≥ 2 KB free RAM

## License

MIT — see the top-level project LICENSE file.
