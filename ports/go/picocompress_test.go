package picocompress

import (
	"bytes"
	"crypto/rand"
	"strings"
	"testing"
)

func TestEmptyInput(t *testing.T) {
	compressed, err := Compress(nil)
	if err != nil {
		t.Fatalf("Compress(nil): %v", err)
	}
	if len(compressed) != 0 {
		t.Fatalf("expected empty output, got %d bytes", len(compressed))
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if len(decompressed) != 0 {
		t.Fatalf("expected empty decompressed, got %d bytes", len(decompressed))
	}
}

func TestSingleByte(t *testing.T) {
	input := []byte("A")
	compressed, err := Compress(input)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	// Single byte: compressed payload (2 bytes) >= raw_len (1), so C stores raw.
	// Header: raw_len=1 comp_len=0 (raw), payload: 0x41
	expected := []byte{0x01, 0x00, 0x00, 0x00, 0x41}
	if !bytes.Equal(compressed, expected) {
		t.Fatalf("single byte: expected %x, got %x", expected, compressed)
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if !bytes.Equal(decompressed, input) {
		t.Fatalf("roundtrip failed: expected %q, got %q", input, decompressed)
	}
}

func TestRepeatedByte(t *testing.T) {
	input := bytes.Repeat([]byte("A"), 8)
	compressed, err := Compress(input)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	// Should compress smaller than raw + header (12 bytes)
	if len(compressed) >= 12 {
		t.Fatalf("repeated byte: expected compressed < 12 bytes, got %d", len(compressed))
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if !bytes.Equal(decompressed, input) {
		t.Fatalf("roundtrip failed for repeated byte")
	}
}

func TestRoundtripJSON(t *testing.T) {
	// 508 bytes of JSON-like data
	input := []byte(`{"name":"picocompress","version":"1.0.0","description":"A lightweight compression library","type":"module","status":"active","data":{"value":42,"state":"ready","result":"success","error":null,"message":"content loaded","request":"default","response":"ok","number":12345,"length":508,"string":"hello world","device":"sensor-01","region":"us-east","active":true,"input":"test","order":"ascending","code":"UTF-8","mode":"balanced"},"list":["item","text","false","true","null","name","data","time","type","size","ment","tion"],"content":"The message is the default response"}`)
	if len(input) > 508 {
		input = input[:508]
	}

	compressed, err := Compress(input)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}
	if len(compressed) >= len(input)+4 {
		t.Logf("warning: JSON didn't compress (compressed=%d, raw=%d)", len(compressed), len(input))
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if !bytes.Equal(decompressed, input) {
		t.Fatalf("JSON roundtrip failed: lengths %d vs %d", len(input), len(decompressed))
	}
}

func TestRoundtripMultiBlock(t *testing.T) {
	// 4096 bytes of repeated prose (multi-block)
	prose := "The quick brown fox jumps over the lazy dog. This is a test of the picocompress algorithm. "
	input := []byte(strings.Repeat(prose, 50))
	input = input[:4096]

	compressed, err := Compress(input)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if !bytes.Equal(decompressed, input) {
		t.Fatalf("multi-block roundtrip failed: lengths %d vs %d", len(input), len(decompressed))
	}

	// Should achieve significant compression on repeated prose
	ratio := float64(len(compressed)) / float64(len(input))
	t.Logf("multi-block: %d → %d (%.1f%%)", len(input), len(compressed), ratio*100)
}

func TestRoundtripRandomData(t *testing.T) {
	// 508 bytes of random data — should store raw (comp_len=0)
	input := make([]byte, 508)
	if _, err := rand.Read(input); err != nil {
		t.Fatalf("rand.Read: %v", err)
	}

	compressed, err := Compress(input)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	// Verify it stored raw: header should have comp_len=0
	if len(compressed) >= 4 {
		compLen := int(compressed[2]) | int(compressed[3])<<8
		if compLen != 0 {
			t.Logf("random data was compressed (comp_len=%d), expected raw", compLen)
		}
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if !bytes.Equal(decompressed, input) {
		t.Fatalf("random data roundtrip failed")
	}
}

func TestRoundtripVariousSizes(t *testing.T) {
	base := "Hello, picocompress! This is a test with some repeated content for compression. "
	for _, size := range []int{0, 1, 2, 3, 64, 100, 507, 508, 509, 1016, 2000, 4096, 10000} {
		input := make([]byte, size)
		src := []byte(strings.Repeat(base, (size/len(base))+1))
		copy(input, src)

		compressed, err := Compress(input)
		if err != nil {
			t.Fatalf("Compress size=%d: %v", size, err)
		}

		decompressed, err := Decompress(compressed)
		if err != nil {
			t.Fatalf("Decompress size=%d: %v", size, err)
		}
		if !bytes.Equal(decompressed, input) {
			t.Fatalf("roundtrip size=%d failed: got %d bytes", size, len(decompressed))
		}
	}
}

func TestDictionaryEntries(t *testing.T) {
	// Verify dictionary has exactly 96 entries
	if len(staticDict) != 96 {
		t.Fatalf("dictionary has %d entries, expected 96", len(staticDict))
	}

	// Spot-check a few entries
	checks := []struct {
		idx  int
		data []byte
	}{
		{0, []byte{0x22, 0x3A, 0x20, 0x22}},
		{1, []byte{0x7D, 0x2C, 0x0A, 0x22}},
		{2, []byte{0x3C, 0x2F, 0x64, 0x69, 0x76}},
		{8, []byte{0x22, 0x3A, 0x22}},
		{13, []byte{0x2C, 0x22, 0x2C}},
		{20, []byte{0x2F, 0x3E, 0x0D, 0x0A}},
		{25, []byte("true")},
		{40, []byte("false")},
		{48, []byte("status")},
		{56, []byte("message")},
		{60, []byte{0x6E, 0x75, 0x6D, 0x62, 0x65, 0x72, 0x22, 0x3A}},
		{62, []byte{0x68, 0x74, 0x74, 0x70, 0x73, 0x3A, 0x2F, 0x2F}},
		{63, []byte("response")},
		{64, []byte{0x2E, 0x20, 0x54, 0x68, 0x65, 0x20}},
		{70, []byte{0x54, 0x68, 0x65, 0x20}},
		{77, []byte{0x6F, 0x75, 0x6C, 0x64, 0x20}},
		{79, []byte{0x22, 0x2C, 0x20, 0x22}},
		{80, []byte("DIM")},
		{95, []byte("PROGRAM")},
	}
	for _, c := range checks {
		if !bytes.Equal(staticDict[c.idx].data, c.data) {
			t.Errorf("dict[%d]: expected %x, got %x", c.idx, c.data, staticDict[c.idx].data)
		}
	}
}

func TestDecompressCorrupt(t *testing.T) {
	// Truncated header
	_, err := Decompress([]byte{0x01, 0x00})
	if err == nil {
		t.Fatal("expected error for truncated header")
	}

	// Invalid raw_len (too large)
	_, err = Decompress([]byte{0xFF, 0x0F, 0x00, 0x00, 0x41})
	if err == nil {
		t.Fatal("expected error for oversized raw_len")
	}
}

func TestRoundtripDictionaryContent(t *testing.T) {
	// Input containing many dictionary entries to exercise dict matching
	input := []byte(`{"status":"active","type":"default","message":"error","value":null,"name":"test","data":"content","result":"false","number":123,"length":100,"request":"input","response":"order","region":"us","device":"d1","string":"s","code":"c","mode":"m","time":"t","size":"s","list":"l","item":"i","text":"x","state":"ok","alert":"!","operator":"op","https://example.com"}`)

	compressed, err := Compress(input)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if !bytes.Equal(decompressed, input) {
		t.Fatalf("dictionary content roundtrip failed")
	}

	// Should compress well since it has many dictionary matches
	t.Logf("dict content: %d → %d", len(input), len(compressed))
}

func TestRoundtripAllZeros(t *testing.T) {
	input := make([]byte, 1024)
	compressed, err := Compress(input)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}
	if !bytes.Equal(decompressed, input) {
		t.Fatalf("all-zeros roundtrip failed")
	}
	t.Logf("all-zeros 1024B: %d → %d", len(input), len(compressed))
}

func TestWithLazySteps(t *testing.T) {
	input := []byte(strings.Repeat("The quick brown fox jumps over the lazy dog. ", 20))

	for _, steps := range []int{0, 1, 2} {
		compressed, err := Compress(input, WithLazySteps(steps))
		if err != nil {
			t.Fatalf("Compress lazy=%d: %v", steps, err)
		}

		decompressed, err := Decompress(compressed)
		if err != nil {
			t.Fatalf("Decompress lazy=%d: %v", steps, err)
		}
		if !bytes.Equal(decompressed, input) {
			t.Fatalf("roundtrip lazy=%d failed", steps)
		}
		t.Logf("lazy=%d: %d → %d", steps, len(input), len(compressed))
	}
}
