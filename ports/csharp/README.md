# picocompress — C# Port

Pure C# (.NET 10) port of the picocompress block-based LZ compression library.
Produces **byte-identical** compressed output to the C reference implementation.

## Features

- Pure managed C# — no `unsafe`, no P/Invoke, zero NuGet runtime dependencies
- `Span<byte>` / `ReadOnlySpan<byte>` API
- Configurable encoder profiles (Micro → Q4)
- Static dictionary with 96 entries (identical to C)
- Cross-block history for multi-block compression

## Quick Start

```csharp
using Picocompress;

// Compress
byte[] input = System.Text.Encoding.UTF8.GetBytes("Hello, picocompress!");
byte[] compressed = PicocompressCodec.Compress(input);

// Decompress
byte[] restored = PicocompressCodec.Decompress(compressed);
// restored is byte-identical to input
```

## API

```csharp
// Compress with default profile (Balanced: block=508, hash=512×2, history=504)
static byte[] PicocompressCodec.Compress(ReadOnlySpan<byte> input,
    PicocompressOptions? options = null);

// Decompress (profile-independent — any encoder profile works)
static byte[] PicocompressCodec.Decompress(ReadOnlySpan<byte> compressed);
```

## Profiles

```csharp
PicocompressCodec.Compress(data, PicocompressOptions.Default);    // Balanced
PicocompressCodec.Compress(data, PicocompressOptions.Micro);      // ~1 KB encoder
PicocompressCodec.Compress(data, PicocompressOptions.Minimal);    // ~1.8 KB encoder
PicocompressCodec.Compress(data, PicocompressOptions.Aggressive); // depth=4
PicocompressCodec.Compress(data, PicocompressOptions.Q3);         // 1024 history
PicocompressCodec.Compress(data, PicocompressOptions.Q4);         // 2048 history
```

## Custom Profile

```csharp
var opts = new PicocompressOptions
{
    BlockSize   = 508,
    HashBits    = 10,
    ChainDepth  = 2,
    HistorySize = 1024,
    LazySteps   = 2
};
byte[] compressed = PicocompressCodec.Compress(data, opts);
```

## Build & Test

```bash
dotnet test
```

## Token Format

| Byte Range   | Type             | Size  | Description                        |
|-------------|------------------|-------|------------------------------------|
| 0x00–0x3F   | Short literal    | 1+N   | Emit N raw bytes (N = 1..64)       |
| 0x40–0x7F   | Dictionary ref   | 1     | Entry 0..63                        |
| 0x80–0xBF   | LZ match         | 2     | 5-bit len + 9-bit offset           |
| 0xC0–0xCF   | Repeat-offset    | 1     | 4-bit len, reuse last offset       |
| 0xD0–0xDF   | Dictionary ref   | 1     | Entry 80..95                       |
| 0xE0–0xEF   | Dictionary ref   | 1     | Entry 64..79                       |
| 0xF0–0xFF   | Long-offset LZ   | 3     | 4-bit len + 16-bit offset BE       |
