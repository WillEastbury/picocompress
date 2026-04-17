# picocompress — Go Port

Pure Go implementation of the [picocompress](https://github.com/WillEastbury/picocompress) block compression format. Produces byte-identical output to the C reference implementation.

## Installation

```bash
go get github.com/WillEastbury/picocompress/ports/go
```

## Usage

```go
package main

import (
	"fmt"
	"log"

	picocompress "github.com/WillEastbury/picocompress/ports/go"
)

func main() {
	input := []byte(`{"name":"example","status":"active","value":42}`)

	// Compress
	compressed, err := picocompress.Compress(input)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("Compressed: %d → %d bytes\n", len(input), len(compressed))

	// Decompress
	restored, err := picocompress.Decompress(compressed)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(string(restored))
}
```

### Functional Options

```go
// Disable lazy matching (faster, slightly worse ratio)
compressed, _ := picocompress.Compress(data, picocompress.WithLazySteps(0))

// More aggressive lazy matching (slower, slightly better ratio)
compressed, _ := picocompress.Compress(data, picocompress.WithLazySteps(2))
```

## API

### `func Compress(input []byte, opts ...Option) ([]byte, error)`

Compresses input using the picocompress format. Returns the compressed bytes. Empty input returns an empty slice with no error.

### `func Decompress(compressed []byte) ([]byte, error)`

Decompresses data produced by `Compress` or the C reference implementation. Returns `ErrCorrupt` if the compressed data is malformed.

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `WithLazySteps(n)` | 1 | Lazy match evaluation steps (0–2) |

## Compatibility

- Pure Go, zero external dependencies
- Byte-identical output to the C reference implementation
- Cross-block history support for multi-block streams
- Static dictionary with all 96 entries
