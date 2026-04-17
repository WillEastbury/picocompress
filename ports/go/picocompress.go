// Package picocompress implements the picocompress block compression format.
//
// picocompress is a lightweight LZ-based compressor designed for embedded
// systems. It uses a static dictionary of 96 entries, short/long LZ matches,
// repeat-offset caching, and lazy match evaluation to achieve good
// compression ratios with minimal memory.
//
// Compressed streams are byte-identical to those produced by the C reference
// implementation.
package picocompress

import (
	"encoding/binary"
	"errors"
)

// Algorithm constants matching the C reference implementation.
const (
	blockSize          = 508
	literalMax         = 64
	matchMin           = 2
	matchCodeBits      = 5
	matchMax           = matchMin + (1<<matchCodeBits - 1) // 33
	offsetShortMax     = (1 << 9) - 1                      // 511
	longMatchMin       = 2
	longMatchMax       = 17
	offsetLongMax      = 65535
	dictCount          = 96
	blockMaxCompressed = blockSize + blockSize/literalMax + 16
	hashBits           = 9
	hashSize           = 1 << hashBits // 512
	hashChainDepth     = 2
	historySize        = 504
	goodMatch          = 8
	repeatCacheSize    = 3
	lazySteps          = 1
)

// Errors returned by Compress and Decompress.
var (
	ErrCorrupt = errors.New("picocompress: corrupt compressed data")
	ErrInput   = errors.New("picocompress: invalid input")
)

// dictEntry holds the bytes and length for a static dictionary entry.
type dictEntry struct {
	data []byte
}

// staticDict is the 96-entry static dictionary. Every implementation MUST use
// this exact dictionary — it is part of the format specification.
var staticDict = [dictCount]dictEntry{
	// 0-3: high-value multi-byte patterns
	{[]byte{0x22, 0x3A, 0x20, 0x22}},             // 0:  ": "
	{[]byte{0x7D, 0x2C, 0x0A, 0x22}},             // 1:  },\n"
	{[]byte{0x3C, 0x2F, 0x64, 0x69, 0x76}},       // 2:  </div
	{[]byte("tion")},                              // 3
	// 4-7: common English suffixes
	{[]byte("ment")},                              // 4
	{[]byte("ness")},                              // 5
	{[]byte("able")},                              // 6
	{[]byte("ight")},                              // 7
	// 8-15: three/four-byte patterns
	{[]byte{0x22, 0x3A, 0x22}},                    // 8:  ":"
	{[]byte{0x3C, 0x2F, 0x64, 0x69}},             // 9:  </di
	{[]byte{0x3D, 0x22, 0x68, 0x74}},             // 10: ="ht
	{[]byte("the")},                               // 11
	{[]byte("ing")},                               // 12
	{[]byte{0x2C, 0x22, 0x2C}},                   // 13: ","
	{[]byte{0x22, 0x3A, 0x7B}},                   // 14: ":{
	{[]byte{0x22, 0x3A, 0x5B}},                   // 15: ":[
	// 16-23
	{[]byte("ion")},                               // 16
	{[]byte("ent")},                               // 17
	{[]byte("ter")},                               // 18
	{[]byte("and")},                               // 19
	{[]byte{0x2F, 0x3E, 0x0D, 0x0A}},             // 20: />\r\n
	{[]byte{0x22, 0x7D, 0x2C}},                   // 21: "},
	{[]byte{0x22, 0x5D, 0x2C}},                   // 22: "],
	{[]byte("have")},                              // 23
	// 24-39: four-byte
	{[]byte{0x6E, 0x6F, 0x22, 0x3A}},             // 24: no":
	{[]byte("true")},                              // 25
	{[]byte("null")},                              // 26
	{[]byte("name")},                              // 27
	{[]byte("data")},                              // 28
	{[]byte("time")},                              // 29
	{[]byte("type")},                              // 30
	{[]byte("mode")},                              // 31
	{[]byte("http")},                              // 32
	{[]byte("tion")},                              // 33
	{[]byte("code")},                              // 34
	{[]byte("size")},                              // 35
	{[]byte("ment")},                              // 36
	{[]byte("list")},                              // 37
	{[]byte("item")},                              // 38
	{[]byte("text")},                              // 39
	// 40-47: five-byte
	{[]byte("false")},                             // 40
	{[]byte("error")},                             // 41
	{[]byte("value")},                             // 42
	{[]byte("state")},                             // 43
	{[]byte("alert")},                             // 44
	{[]byte("input")},                             // 45
	{[]byte("ation")},                             // 46
	{[]byte("order")},                             // 47
	// 48-55: six-byte
	{[]byte("status")},                            // 48
	{[]byte("number")},                            // 49
	{[]byte("active")},                            // 50
	{[]byte("device")},                            // 51
	{[]byte("region")},                            // 52
	{[]byte("string")},                            // 53
	{[]byte("result")},                            // 54
	{[]byte("length")},                            // 55
	// 56-59: seven-byte
	{[]byte("message")},                           // 56
	{[]byte("content")},                           // 57
	{[]byte("request")},                           // 58
	{[]byte("default")},                           // 59
	// 60-63: eight-byte
	{[]byte{0x6E, 0x75, 0x6D, 0x62, 0x65, 0x72, 0x22, 0x3A}}, // 60: number":
	{[]byte("operator")},                          // 61
	{[]byte{0x68, 0x74, 0x74, 0x70, 0x73, 0x3A, 0x2F, 0x2F}}, // 62: https://
	{[]byte("response")},                          // 63
	// 64-67: sentence starters
	{[]byte{0x2E, 0x20, 0x54, 0x68, 0x65, 0x20}},             // 64: ". The "
	{[]byte{0x2E, 0x20, 0x49, 0x74, 0x20}},                   // 65: ". It "
	{[]byte{0x2E, 0x20, 0x54, 0x68, 0x69, 0x73, 0x20}},       // 66: ". This "
	{[]byte{0x2E, 0x20, 0x41, 0x20}},                         // 67: ". A "
	// 68-71: capitalized terms
	{[]byte("HTTP")},                              // 68
	{[]byte("JSON")},                              // 69
	{[]byte{0x54, 0x68, 0x65, 0x20}},             // 70: "The "
	{[]byte("None")},                              // 71
	// 72-75: phoneme patterns
	{[]byte("ment")},                              // 72
	{[]byte("ness")},                              // 73
	{[]byte("able")},                              // 74
	{[]byte("ight")},                              // 75
	// 76-79: phoneme + structural
	{[]byte("ation")},                             // 76
	{[]byte{0x6F, 0x75, 0x6C, 0x64, 0x20}},       // 77: "ould "
	{[]byte{0x22, 0x3A, 0x20, 0x22}},             // 78: ": "
	{[]byte{0x22, 0x2C, 0x20, 0x22}},             // 79: ", "
	// 80-95: uppercase keyword primitives
	{[]byte("DIM")},                               // 80
	{[]byte("FOR")},                               // 81
	{[]byte("END")},                               // 82
	{[]byte("REL")},                               // 83
	{[]byte("EACH")},                              // 84
	{[]byte("LOAD")},                              // 85
	{[]byte("SAVE")},                              // 86
	{[]byte("CARD")},                              // 87
	{[]byte("JUMP")},                              // 88
	{[]byte("PRINT")},                             // 89
	{[]byte("INPUT")},                             // 90
	{[]byte("GOSUB")},                             // 91
	{[]byte("STREAM")},                            // 92
	{[]byte("RETURN")},                            // 93
	{[]byte("SWITCH")},                            // 94
	{[]byte("PROGRAM")},                           // 95
}

// options holds configurable compression parameters.
type options struct {
	blockSize  int
	lazySteps  int
	histSize   int
	hashBits   int
	chainDepth int
}

func defaultOptions() options {
	return options{
		blockSize:  blockSize,
		lazySteps:  lazySteps,
		histSize:   historySize,
		hashBits:   hashBits,
		chainDepth: hashChainDepth,
	}
}

// Option configures compression behaviour via functional options.
type Option func(*options)

// WithLazySteps sets the number of lazy evaluation steps (0, 1, or 2).
// More steps can improve compression ratio at the cost of speed.
func WithLazySteps(n int) Option {
	return func(o *options) {
		if n >= 0 && n <= 2 {
			o.lazySteps = n
		}
	}
}

// Compress compresses input using the picocompress format. The output is
// byte-identical to the C reference implementation. Options may be provided
// to tune compression behaviour.
func Compress(input []byte, opts ...Option) ([]byte, error) {
	o := defaultOptions()
	for _, fn := range opts {
		fn(&o)
	}

	if len(input) == 0 {
		return []byte{}, nil
	}

	out := make([]byte, 0, compressBound(len(input)))
	history := make([]byte, 0, historySize)

	pos := 0
	for pos < len(input) {
		end := pos + blockSize
		if end > len(input) {
			end = len(input)
		}
		block := input[pos:end]
		rawLen := len(block)

		// Build virtual buffer: [history | block]
		vbuf := make([]byte, len(history)+rawLen)
		copy(vbuf, history)
		copy(vbuf[len(history):], block)
		histLen := uint16(len(history))

		compressed := compressBlock(vbuf, histLen, uint16(rawLen), o)

		// Write 4-byte little-endian header
		var hdr [4]byte
		binary.LittleEndian.PutUint16(hdr[0:2], uint16(rawLen))
		if compressed == nil || len(compressed) >= rawLen {
			// Store raw
			binary.LittleEndian.PutUint16(hdr[2:4], 0)
			out = append(out, hdr[:]...)
			out = append(out, block...)
		} else {
			binary.LittleEndian.PutUint16(hdr[2:4], uint16(len(compressed)))
			out = append(out, hdr[:]...)
			out = append(out, compressed...)
		}

		history = updateHistory(history, block)
		pos = end
	}

	return out, nil
}

// Decompress decompresses data produced by Compress (or the C reference).
func Decompress(compressed []byte) ([]byte, error) {
	if len(compressed) == 0 {
		return []byte{}, nil
	}

	out := make([]byte, 0, len(compressed)*2)
	history := make([]byte, 0, historySize)
	pos := 0

	for pos < len(compressed) {
		if pos+4 > len(compressed) {
			return nil, ErrCorrupt
		}
		rawLen := int(binary.LittleEndian.Uint16(compressed[pos : pos+2]))
		compLen := int(binary.LittleEndian.Uint16(compressed[pos+2 : pos+4]))
		pos += 4

		if rawLen == 0 && compLen == 0 {
			continue
		}
		if rawLen == 0 || rawLen > blockSize {
			return nil, ErrCorrupt
		}

		if compLen == 0 {
			// Raw block
			if pos+rawLen > len(compressed) {
				return nil, ErrCorrupt
			}
			blockData := compressed[pos : pos+rawLen]
			out = append(out, blockData...)
			history = updateHistory(history, blockData)
			pos += rawLen
		} else {
			// Compressed block
			if compLen > blockMaxCompressed {
				return nil, ErrCorrupt
			}
			if pos+compLen > len(compressed) {
				return nil, ErrCorrupt
			}
			payload := compressed[pos : pos+compLen]
			blockData, err := decompressBlock(history, payload, rawLen)
			if err != nil {
				return nil, err
			}
			out = append(out, blockData...)
			history = updateHistory(history, blockData)
			pos += compLen
		}
	}

	return out, nil
}

// compressBound returns the maximum compressed size for a given input length.
func compressBound(inputLen int) int {
	if inputLen == 0 {
		return 0
	}
	blocks := (inputLen + blockSize - 1) / blockSize
	return inputLen + blocks*4
}

// updateHistory appends blockData to history, keeping at most historySize bytes.
func updateHistory(history, blockData []byte) []byte {
	bLen := len(blockData)
	hLen := len(history)

	if bLen >= historySize {
		result := make([]byte, historySize)
		copy(result, blockData[bLen-historySize:])
		return result
	}
	if hLen+bLen <= historySize {
		result := make([]byte, hLen+bLen)
		copy(result, history)
		copy(result[hLen:], blockData)
		return result
	}
	keep := historySize - bLen
	if keep > hLen {
		keep = hLen
	}
	result := make([]byte, keep+bLen)
	copy(result, history[hLen-keep:])
	copy(result[keep:], blockData)
	return result
}

// --- Decompressor ---

// decompressBlock decodes a compressed block payload into raw bytes.
func decompressBlock(history, payload []byte, expectedLen int) ([]byte, error) {
	out := make([]byte, expectedLen)
	ip := 0
	op := 0
	lastOffset := 0
	histLen := len(history)

	for ip < len(payload) {
		token := payload[ip]
		ip++

		switch {
		case token < 0x40:
			// Short literal: copy N raw bytes
			litLen := int(token&0x3F) + 1
			if ip+litLen > len(payload) || op+litLen > expectedLen {
				return nil, ErrCorrupt
			}
			copy(out[op:], payload[ip:ip+litLen])
			ip += litLen
			op += litLen

		case token < 0x80:
			// Dictionary ref (entries 0..63)
			idx := int(token & 0x3F)
			if idx >= dictCount {
				return nil, ErrCorrupt
			}
			entry := staticDict[idx].data
			if op+len(entry) > expectedLen {
				return nil, ErrCorrupt
			}
			copy(out[op:], entry)
			op += len(entry)

		case token < 0xC0:
			// Short LZ match
			if ip >= len(payload) {
				return nil, ErrCorrupt
			}
			matchLen := int((token>>1)&0x1F) + matchMin
			offset := (int(token&0x01) << 8) | int(payload[ip])
			ip++
			if offset == 0 {
				return nil, ErrCorrupt
			}
			if offset > op+histLen {
				return nil, ErrCorrupt
			}
			if op+matchLen > expectedLen {
				return nil, ErrCorrupt
			}
			copyMatch(out, &op, history, histLen, offset, matchLen)
			lastOffset = offset

		case token < 0xD0:
			// Repeat-offset match
			matchLen := int(token&0x0F) + matchMin
			if lastOffset == 0 {
				return nil, ErrCorrupt
			}
			if lastOffset > op+histLen {
				return nil, ErrCorrupt
			}
			if op+matchLen > expectedLen {
				return nil, ErrCorrupt
			}
			copyMatch(out, &op, history, histLen, lastOffset, matchLen)

		case token < 0xE0:
			// Dictionary ref (entries 80..95)
			idx := 80 + int(token&0x0F)
			if idx >= dictCount {
				return nil, ErrCorrupt
			}
			entry := staticDict[idx].data
			if op+len(entry) > expectedLen {
				return nil, ErrCorrupt
			}
			copy(out[op:], entry)
			op += len(entry)

		case token < 0xF0:
			// Dictionary ref (entries 64..79)
			idx := 64 + int(token&0x0F)
			if idx >= dictCount {
				return nil, ErrCorrupt
			}
			entry := staticDict[idx].data
			if op+len(entry) > expectedLen {
				return nil, ErrCorrupt
			}
			copy(out[op:], entry)
			op += len(entry)

		default:
			// Long LZ match (0xF0..0xFF)
			matchLen := int(token&0x0F) + longMatchMin
			if ip+2 > len(payload) {
				return nil, ErrCorrupt
			}
			// Big-endian offset
			offset := (int(payload[ip]) << 8) | int(payload[ip+1])
			ip += 2
			if offset == 0 {
				return nil, ErrCorrupt
			}
			if offset > op+histLen {
				return nil, ErrCorrupt
			}
			if op+matchLen > expectedLen {
				return nil, ErrCorrupt
			}
			copyMatch(out, &op, history, histLen, offset, matchLen)
			lastOffset = offset
		}
	}

	if op != expectedLen {
		return nil, ErrCorrupt
	}
	return out, nil
}

// copyMatch copies matchLen bytes from offset positions back, handling
// cross-block history references and overlapping copies byte-by-byte.
func copyMatch(out []byte, op *int, history []byte, histLen, offset, matchLen int) {
	o := *op
	if offset <= o {
		// Entirely within current block output — byte-by-byte for overlap safety.
		src := o - offset
		for i := 0; i < matchLen; i++ {
			out[o+i] = out[src+i]
		}
	} else {
		// Starts in history, may cross into current output.
		histBack := offset - o
		histStart := histLen - histBack
		for i := 0; i < matchLen; i++ {
			srcIdx := histStart + i
			if srcIdx < histLen {
				out[o+i] = history[srcIdx]
			} else {
				out[o+i] = out[srcIdx-histLen]
			}
		}
	}
	*op = o + matchLen
}

// --- Compressor ---

// hash3 computes the portable multiply hash over 3 bytes at p.
func hash3(data []byte, pos int) uint16 {
	v := uint32(data[pos])*251 + uint32(data[pos+1])*11 + uint32(data[pos+2])*3
	return uint16(v & (hashSize - 1))
}

// headInsert inserts pos into the hash chain, shifting older entries down.
func headInsert(head *[hashChainDepth][hashSize]int16, hash uint16, pos int16) {
	for d := hashChainDepth - 1; d > 0; d-- {
		head[d][hash] = head[d-1][hash]
	}
	head[0][hash] = pos
}

// matchLen counts equal bytes starting at a and b, up to limit.
func matchLen(data []byte, a, b, limit int) int {
	m := 0
	for m < limit && data[a+m] == data[b+m] {
		m++
	}
	return m
}

// emitLiterals writes literal tokens for src into dst starting at *op.
// Returns false if dst capacity is exceeded.
func emitLiterals(src []byte, dst []byte, op *int) bool {
	pos := 0
	for pos < len(src) {
		chunk := len(src) - pos
		if chunk > literalMax {
			chunk = literalMax
		}
		if *op+1+chunk > len(dst) {
			return false
		}
		dst[*op] = byte(chunk - 1) // 0x00..0x3F
		*op++
		copy(dst[*op:], src[pos:pos+chunk])
		*op += chunk
		pos += chunk
	}
	return true
}

// findBest tries repeat-cache, dictionary, and LZ hash chain at vpos.
// Returns net savings. Fills out parameters.
func findBest(
	vbuf []byte, vbufLen, vpos int,
	head *[hashChainDepth][hashSize]int16,
	repOffsets [repeatCacheSize]uint16,
	gm int,
	skipDict bool,
) (bestSavings int, outLen, outOff uint16, outDict uint16, outIsRepeat bool) {
	remaining := vbufLen - vpos
	outDict = 0xFFFF

	// 1. Repeat-offset cache
	if remaining >= matchMin {
		maxRep := remaining
		if maxRep > matchMax {
			maxRep = matchMax
		}
		for d := 0; d < repeatCacheSize; d++ {
			off := int(repOffsets[d])
			if off == 0 || off > vpos {
				continue
			}
			// Early reject: first byte
			if vbuf[vpos] != vbuf[vpos-off] {
				continue
			}
			// Early reject: second byte
			if remaining >= 2 && vbuf[vpos+1] != vbuf[vpos-off+1] {
				continue
			}
			mLen := matchLen(vbuf, vpos-off, vpos, maxRep)
			if mLen < matchMin {
				continue
			}

			isRep := d == 0 && mLen <= 17
			tokenCost := 1
			if !isRep {
				if off <= offsetShortMax {
					tokenCost = 2
				} else {
					tokenCost = 3
				}
			}
			s := mLen - tokenCost

			if s > bestSavings {
				bestSavings = s
				outLen = uint16(mLen)
				outOff = uint16(off)
				outDict = 0xFFFF
				outIsRepeat = isRep
				if mLen >= gm {
					return
				}
			}
		}
	}

	// 2. Dictionary match
	if !skipDict {
		firstByte := vbuf[vpos]
		for d := 0; d < dictCount; d++ {
			entry := staticDict[d].data
			dlen := len(entry)
			if dlen > remaining {
				continue
			}
			if dlen-1 <= bestSavings {
				continue
			}
			if entry[0] != firstByte {
				continue
			}
			match := true
			for k := 1; k < dlen; k++ {
				if vbuf[vpos+k] != entry[k] {
					match = false
					break
				}
			}
			if !match {
				continue
			}
			bestSavings = dlen - 1
			outDict = uint16(d)
			outLen = uint16(dlen)
			outOff = 0
			outIsRepeat = false
			if dlen >= gm {
				return
			}
		}
	}

	// 3. LZ hash-chain match
	if remaining >= 3 {
		h := hash3(vbuf, vpos)
		maxLenShort := remaining
		if maxLenShort > matchMax {
			maxLenShort = matchMax
		}
		maxLenLong := remaining
		if maxLenLong > longMatchMax {
			maxLenLong = longMatchMax
		}
		fb := vbuf[vpos]

		for d := 0; d < hashChainDepth; d++ {
			prev := head[d][h]
			if prev < 0 {
				continue
			}
			prevPos := int(prev)
			if prevPos >= vpos {
				continue
			}
			off := vpos - prevPos
			if off == 0 || off > offsetLongMax {
				continue
			}
			// Early reject: first byte
			if vbuf[prevPos] != fb {
				continue
			}

			maxL := maxLenShort
			if off > offsetShortMax {
				maxL = maxLenLong
			}
			mLen := matchLen(vbuf, prevPos, vpos, maxL)
			if mLen < matchMin {
				continue
			}

			tokenCost := 2
			if off > offsetShortMax {
				tokenCost = 3
			}
			s := mLen - tokenCost

			// Offset scoring: prefer nearer matches at equal savings,
			// longer matches, and long-offset length bonus.
			if s > bestSavings ||
				(s == bestSavings && mLen > int(outLen)) ||
				(s == bestSavings && mLen == int(outLen) && off < int(outOff)) ||
				(s == bestSavings-1 && mLen >= int(outLen)+2) {
				bestSavings = mLen - tokenCost
				outLen = uint16(mLen)
				outOff = uint16(off)
				outDict = 0xFFFF
				outIsRepeat = false
				if mLen >= gm {
					return
				}
			}
		}
	}

	return
}

// compressBlock compresses a single block within vbuf.
// vbuf = [history(histLen) | block(blockLen)].
// Returns the compressed payload, or nil if compression doesn't save space.
func compressBlock(vbuf []byte, histLen, blockLen uint16, o options) []byte {
	var head [hashChainDepth][hashSize]int16
	var repOffsets [repeatCacheSize]uint16
	vbufLen := int(histLen) + int(blockLen)

	// Initialize hash table to -1
	for d := 0; d < hashChainDepth; d++ {
		for i := 0; i < hashSize; i++ {
			head[d][i] = -1
		}
	}

	// Seed hash table from history
	if histLen >= 3 {
		for p := 0; p+2 < int(histLen); p++ {
			headInsert(&head, hash3(vbuf, p), int16(p))
		}
		// Boundary boost: re-inject last 64 positions
		tailStart := 0
		if int(histLen) > 64 {
			tailStart = int(histLen) - 64
		}
		for p := tailStart; p+2 < int(histLen); p++ {
			h := hash3(vbuf, p)
			if head[0][h] != int16(p) {
				save := head[hashChainDepth-1][h]
				headInsert(&head, h, int16(p))
				head[hashChainDepth-1][h] = save
			}
		}
	}

	// Self-disabling dictionary heuristic
	dictSkip := false
	if blockLen >= 1 {
		b0 := vbuf[histLen]
		if b0 == '{' || b0 == '[' || b0 == '<' || b0 == 0xEF {
			dictSkip = false
		} else {
			checkLen := int(blockLen)
			if checkLen > 4 {
				checkLen = 4
			}
			for ci := 0; ci < checkLen; ci++ {
				c := vbuf[int(histLen)+ci]
				if c < 0x20 || c > 0x7E {
					dictSkip = true
					break
				}
			}
		}
	}

	out := make([]byte, blockMaxCompressed)
	op := 0
	anchor := int(histLen)
	vpos := int(histLen)

	for vpos < vbufLen {
		if vbufLen-vpos < matchMin {
			break
		}

	retryPos:
		bestSavings, bestLen, bestOff, bestDict, bestIsRepeat := findBest(
			vbuf, vbufLen, vpos, &head, repOffsets, goodMatch, dictSkip,
		)

		// Insert current position into hash table
		if vbufLen-vpos >= 3 {
			headInsert(&head, hash3(vbuf, vpos), int16(vpos))
		}

		// Literal run extension: skip weak matches mid-literal-run
		if bestSavings <= 1 && bestDict == 0xFFFF && anchor < vpos {
			bestSavings = 0
		}

		// Lazy matching
		if bestSavings > 0 && int(bestLen) < goodMatch {
			for step := 1; step <= o.lazySteps; step++ {
				npos := vpos + step
				if npos >= vbufLen || vbufLen-npos < matchMin {
					break
				}
				nSav, _, _, _, _ := findBest(
					vbuf, vbufLen, npos, &head, repOffsets, goodMatch, dictSkip,
				)
				if nSav > bestSavings {
					for s := 0; s < step; s++ {
						sp := vpos + s
						if vbufLen-sp >= 3 {
							headInsert(&head, hash3(vbuf, sp), int16(sp))
						}
					}
					vpos = npos
					goto retryPos
				}
			}
		}

		// Emit tokens
		if bestSavings > 0 {
			// Emit pending literals
			litData := vbuf[anchor:vpos]
			if len(litData) > 0 {
				if !emitLiterals(litData, out, &op) {
					return nil // overflow
				}
			}

			if bestDict != 0xFFFF {
				// Dictionary token
				if op+1 > len(out) {
					return nil
				}
				d := int(bestDict)
				if d < 64 {
					out[op] = byte(0x40 | (d & 0x3F))
				} else if d < 80 {
					out[op] = byte(0xE0 | ((d - 64) & 0x0F))
				} else {
					out[op] = byte(0xD0 | ((d - 80) & 0x0F))
				}
				op++
			} else if bestIsRepeat {
				// Repeat-offset token
				if op+1 > len(out) {
					return nil
				}
				out[op] = byte(0xC0 | ((int(bestLen) - matchMin) & 0x0F))
				op++
			} else if int(bestOff) <= offsetShortMax && int(bestLen) <= matchMax {
				// Short LZ match (2 bytes)
				if op+2 > len(out) {
					return nil
				}
				lenCode := int(bestLen) - matchMin
				out[op] = byte(0x80 | ((lenCode & 0x1F) << 1) | ((int(bestOff) >> 8) & 0x01))
				op++
				out[op] = byte(int(bestOff) & 0xFF)
				op++
			} else {
				// Long LZ match (3 bytes, big-endian offset)
				eLen := int(bestLen)
				if eLen > longMatchMax {
					eLen = longMatchMax
				}
				if op+3 > len(out) {
					return nil
				}
				out[op] = byte(0xF0 | ((eLen - longMatchMin) & 0x0F))
				op++
				out[op] = byte((int(bestOff) >> 8) & 0xFF)
				op++
				out[op] = byte(int(bestOff) & 0xFF)
				op++
				bestLen = uint16(eLen)
			}

			// Update repeat-offset cache (only for non-repeat, non-dict LZ matches)
			if !bestIsRepeat && bestOff != 0 && bestDict == 0xFFFF {
				repOffsets[2] = repOffsets[1]
				repOffsets[1] = repOffsets[0]
				repOffsets[0] = bestOff
			}

			// Insert skipped positions into hash table
			for k := 1; k < int(bestLen) && vpos+k+2 < vbufLen; k++ {
				headInsert(&head, hash3(vbuf, vpos+k), int16(vpos+k))
			}

			vpos += int(bestLen)
			anchor = vpos
		} else {
			vpos++
		}
	}

	// Flush remaining literals
	if anchor < vbufLen {
		litData := vbuf[anchor:vbufLen]
		if !emitLiterals(litData, out, &op) {
			return nil
		}
	}

	return out[:op]
}
