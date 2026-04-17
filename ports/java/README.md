# picocompress — Java port

Pure-Java 17+ port of the [picocompress](../../README.md) block-based LZ compression library.
Zero external dependencies. Produces **byte-identical** compressed output to the C reference implementation (portable hash path).

## Quick start

```java
import picocompress.Picocompress;

byte[] raw = "Hello, World!".getBytes();
byte[] compressed = Picocompress.compress(raw);
byte[] restored  = Picocompress.decompress(compressed);
// restored is identical to raw
```

## API

### `Picocompress.compress(byte[] input)`

Compress using the default **Balanced** profile (508-byte blocks, 512×2 hash, 504-byte history).

### `Picocompress.compress(byte[] input, Options options)`

Compress using a specific profile:

```java
import picocompress.Picocompress.Options;

// Built-in profiles (smallest → largest RAM)
byte[] c1 = Picocompress.compress(data, Options.MICRO);       // ~1.0 KB encode
byte[] c2 = Picocompress.compress(data, Options.MINIMAL);     // ~1.8 KB encode
byte[] c3 = Picocompress.compress(data, Options.BALANCED);    // ~4.6 KB encode (default)
byte[] c4 = Picocompress.compress(data, Options.AGGRESSIVE);  // ~4.6 KB encode
byte[] c5 = Picocompress.compress(data, Options.Q3);          // ~7.7 KB encode
byte[] c6 = Picocompress.compress(data, Options.Q4);          // ~13.8 KB encode

// Custom profile
Options custom = new Options(
    /* blockSize */      508,
    /* hashBits */       10,
    /* hashChainDepth */ 3,
    /* historySize */    1024,
    /* lazySteps */      2
);
byte[] c7 = Picocompress.compress(data, custom);
```

### `Picocompress.decompress(byte[] compressed)`

Decompress any picocompress-encoded stream. The decoder is profile-independent — it
works with data compressed by any profile.

```java
byte[] original = Picocompress.decompress(compressed);
```

Throws `IllegalArgumentException` on corrupt or truncated data.

## Maven

Add to your `pom.xml`:

```xml
<dependency>
    <groupId>io.github.picocompress</groupId>
    <artifactId>picocompress</artifactId>
    <version>1.0.0</version>
</dependency>
```

Or build from source:

```bash
cd ports/java
mvn package        # build JAR
mvn test           # run tests
```

## Profiles

| Profile    | Block | Hash     | Depth | History | Lazy | Enc RAM   |
|------------|------:|----------|------:|--------:|-----:|----------:|
| MICRO      |   192 | 256×1    |     1 |      64 |    1 |  ~1.0 KB  |
| MINIMAL    |   508 | 256×1    |     1 |     128 |    1 |  ~1.8 KB  |
| BALANCED   |   508 | 512×2    |     2 |     504 |    1 |  ~4.6 KB  |
| AGGRESSIVE |   508 | 256×4    |     4 |     504 |    1 |  ~4.6 KB  |
| Q3         |   508 | 1024×2   |     2 |    1024 |    2 |  ~7.7 KB  |
| Q4         |   508 | 2048×2   |     2 |    2048 |    2 | ~13.8 KB  |

## Compatibility

- **Java 17+** required (no external dependencies)
- Compressed output is byte-identical to the C reference (portable hash path) and the C# port
- Any profile's output can be decompressed by any decoder (C, C#, Rust, Java)
