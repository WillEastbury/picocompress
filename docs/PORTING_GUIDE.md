# picocompress — Porting Guide & Algorithm Reference

This document provides everything needed to implement a picocompress-compatible
compressor and decompressor in any programming language. A correct implementation
MUST produce byte-identical output to the C reference for all inputs.

---

## Table of contents

1. [Stream format](#1-stream-format)
2. [Token format (byte-level specification)](#2-token-format)
3. [Static dictionary (96 entries)](#3-static-dictionary)
4. [Decompressor (required for all ports)](#4-decompressor)
5. [Compressor](#5-compressor)
6. [Cross-block history](#6-cross-block-history)
7. [Encoder optimisations](#7-encoder-optimisations)
8. [Profiles](#8-profiles)
9. [Verification test vectors](#9-verification-test-vectors)
10. [Common porting pitfalls](#10-common-porting-pitfalls)

---

## 1. Stream format

A picocompress stream is a sequence of **blocks**. Each block has a 4-byte
little-endian header followed by payload bytes:

```
┌─────────────────────────────────────────────┐
│  Stream                                     │
│                                             │
│  ┌────────┬────────────────────┐            │
│  │ Header │ Payload            │  Block 0   │
│  │ 4 bytes│ raw_len or comp_len│            │
│  └────────┴────────────────────┘            │
│  ┌────────┬────────────────────┐            │
│  │ Header │ Payload            │  Block 1   │
│  └────────┴────────────────────┘            │
│  ...                                        │
└─────────────────────────────────────────────┘
```

### Block header (4 bytes, little-endian)

```
Offset  Size   Field       Description
──────  ────   ─────       ───────────
0       u16    raw_len     Original uncompressed size of this block (1..508)
2       u16    comp_len    Compressed payload size; 0 = stored raw (uncompressed)
```

- If `comp_len == 0`: the next `raw_len` bytes are the **raw block data**
  (compression did not reduce size, so it was stored verbatim).
- If `comp_len > 0`: the next `comp_len` bytes are **compressed tokens**
  that decode back to `raw_len` bytes.

The default block size is **508 bytes** (`PC_BLOCK_SIZE`). The maximum is 511
(limited by the 9-bit short offset). Inputs larger than one block are split
into multiple blocks. The last block may be shorter.

### Why 508?

508 = 512 − 4 (header). A full block + header fits in a 512-byte flash page.
This avoids page-crossing overhead on embedded devices.

---

## 2. Token format

Within a compressed block, data is encoded as a sequence of tokens.
Each token starts with a **type byte** whose high bits determine the type:

```
┌──────────────┬───────────────────┬──────┬─────────────────────────────────┐
│ Byte range   │ Type              │ Size │ Description                     │
├──────────────┼───────────────────┼──────┼─────────────────────────────────┤
│ 0x00..0x3F   │ Short literal     │ 1+N  │ Copy N raw bytes (N = 1..64)    │
│ 0x40..0x7F   │ Dictionary ref    │ 1    │ Emit dict entry 0..63           │
│ 0x80..0xBF   │ Short LZ match    │ 2    │ len 2..33, offset 1..511        │
│ 0xC0..0xCF   │ Repeat-offset     │ 1    │ len 2..17, reuse last offset    │
│ 0xD0..0xDF   │ Dictionary ref    │ 1    │ Emit dict entry 80..95          │
│ 0xE0..0xEF   │ Dictionary ref    │ 1    │ Emit dict entry 64..79          │
│ 0xF0..0xFF   │ Long LZ match     │ 3    │ len 2..17, offset 1..65535      │
└──────────────┴───────────────────┴──────┴─────────────────────────────────┘
```

### 2.1 Short literal (0x00..0x3F)

```
Byte 0:  0 0 L5 L4 L3 L2 L1 L0     (L = literal_length − 1)
Bytes 1..N: raw literal data

literal_length = (token & 0x3F) + 1     →  range 1..64
```

The next `literal_length` bytes are copied verbatim to the output.

**Diagram:**
```
Token byte         Raw bytes (copied to output)
┌──────────┐      ┌──┬──┬──┬──┬──┐
│ 0x04     │ ───► │ H│ e│ l│ l│ o│   (5 literal bytes: 0x04 = len 5)
└──────────┘      └──┴──┴──┴──┴──┘
```

### 2.2 Dictionary reference (0x40..0x7F) — entries 0..63

```
Byte 0:  0 1 I5 I4 I3 I2 I1 I0

index = token & 0x3F                    →  range 0..63
emit  = DICTIONARY[index]              (1..8 bytes)
```

A single byte expands to 1–8 bytes from the static dictionary (see §3).

### 2.3 Short LZ match (0x80..0xBF)

```
Byte 0:  1 0 L4 L3 L2 L1 L0 O8       ← 5-bit length + high offset bit
Byte 1:  O7 O6 O5 O4 O3 O2 O1 O0     ← low 8 bits of offset

match_length = ((token >> 1) & 0x1F) + 2    →  range 2..33
offset       = ((token & 0x01) << 8) | byte1  →  range 1..511
```

Copy `match_length` bytes from `output_position − offset`.

**Diagram:**
```
Output so far:    [ A B C D E F G H ... ]
                                  ▲
LZ match: offset=3, len=4        │
                      ┌───────────┘
                      ▼
Copy from:        [ F G H F ]  ← note: overlapping copy is valid!
```

**IMPORTANT:** If `offset < match_length`, the copy overlaps with itself.
You MUST copy byte-by-byte (not memcpy), as later bytes depend on earlier
ones within the same match. This is how LZ run-length encoding works:

```
offset=1, len=5:  copies the byte at (pos-1) five times  →  "AAAAA"
```

### 2.4 Repeat-offset match (0xC0..0xCF)

```
Byte 0:  1 1 0 0 L3 L2 L1 L0

match_length = (token & 0x0F) + 2       →  range 2..17
offset       = (reuse the last LZ offset seen in this block)
```

This saves 1 byte vs a full LZ token. The decoder must track the most recent
LZ offset (from 0x80..0xBF or 0xF0..0xFF tokens). If no LZ match has been
seen yet in this block, a repeat-offset token is invalid (corrupt stream).

### 2.5 Dictionary reference (0xD0..0xDF) — entries 80..95

```
Byte 0:  1 1 0 1 I3 I2 I1 I0

index = 80 + (token & 0x0F)             →  range 80..95
emit  = DICTIONARY[index]
```

### 2.6 Dictionary reference (0xE0..0xEF) — entries 64..79

```
Byte 0:  1 1 1 0 I3 I2 I1 I0

index = 64 + (token & 0x0F)             →  range 64..79
emit  = DICTIONARY[index]
```

### 2.7 Long LZ match (0xF0..0xFF)

```
Byte 0:  1 1 1 1 L3 L2 L1 L0
Byte 1:  O15..O8                        ← offset high byte (BIG-endian)
Byte 2:  O7..O0                         ← offset low byte

match_length = (token & 0x0F) + 2       →  range 2..17
offset       = (byte1 << 8) | byte2     →  range 1..65535
```

Note the 2-byte offset is **big-endian** (high byte first). This differs from
the block header which is little-endian.

---

## 3. Static dictionary

The dictionary has **96 entries** (indices 0..95). Every implementation MUST
use this exact dictionary — it is part of the format specification.

Entries are grouped into three token ranges:
- Indices 0..63 → token `0x40 + index`
- Indices 64..79 → token `0xE0 + (index − 64)`
- Indices 80..95 → token `0xD0 + (index − 80)`

```
Index  Bytes  Content (shown as escaped string or hex)
─────  ─────  ────────────────────────────────────────
  0      4    ": "     (0x22 0x3A 0x20 0x22)
  1      4    },\n"    (0x7D 0x2C 0x0A 0x22)
  2      5    </div    (0x3C 0x2F 0x64 0x69 0x76)
  3      4    tion
  4      4    ment
  5      4    ness
  6      4    able
  7      4    ight
  8      3    ":"      (0x22 0x3A 0x22)
  9      4    </di     (0x3C 0x2F 0x64 0x69)
 10      4    ="ht     (0x3D 0x22 0x68 0x74)
 11      3    the
 12      3    ing
 13      3    ","      (0x2C 0x22 0x2C)
 14      3    ":{      (0x22 0x3A 0x7B)
 15      3    ":[      (0x22 0x3A 0x5B)
 16      3    ion
 17      3    ent
 18      3    ter
 19      3    and
 20      4    />\r\n   (0x2F 0x3E 0x0D 0x0A)
 21      3    "},      (0x22 0x7D 0x2C)
 22      3    "],      (0x22 0x5D 0x2C)
 23      4    have
 24      4    no":     (0x6E 0x6F 0x22 0x3A)
 25      4    true
 26      4    null
 27      4    name
 28      4    data
 29      4    time
 30      4    type
 31      4    mode
 32      4    http
 33      4    tion
 34      4    code
 35      4    size
 36      4    ment
 37      4    list
 38      4    item
 39      4    text
 40      5    false
 41      5    error
 42      5    value
 43      5    state
 44      5    alert
 45      5    input
 46      5    ation
 47      5    order
 48      6    status
 49      6    number
 50      6    active
 51      6    device
 52      6    region
 53      6    string
 54      6    result
 55      6    length
 56      7    message
 57      7    content
 58      7    request
 59      7    default
 60      8    number": (0x6E 0x75 0x6D 0x62 0x65 0x72 0x22 0x3A)
 61      8    operator
 62      8    https:// (0x68 0x74 0x74 0x70 0x73 0x3A 0x2F 0x2F)
 63      8    response
 64      6    . The    (0x2E 0x20 0x54 0x68 0x65 0x20)
 65      5    . It     (0x2E 0x20 0x49 0x74 0x20)
 66      7    . This   (0x2E 0x20 0x54 0x68 0x69 0x73 0x20)
 67      4    . A      (0x2E 0x20 0x41 0x20)
 68      4    HTTP
 69      4    JSON
 70      4    The      (0x54 0x68 0x65 0x20)   ← with trailing space
 71      4    None
 72      4    ment
 73      4    ness
 74      4    able
 75      4    ight
 76      5    ation
 77      5    ould     (0x6F 0x75 0x6C 0x64 0x20)   ← with trailing space
 78      4    ": "     (0x22 0x3A 0x20 0x22)
 79      4    ", "     (0x22 0x2C 0x20 0x22)
 80      3    DIM
 81      3    FOR
 82      3    END
 83      3    REL
 84      4    EACH
 85      4    LOAD
 86      4    SAVE
 87      4    CARD
 88      4    JUMP
 89      5    PRINT
 90      5    INPUT
 91      5    GOSUB
 92      6    STREAM
 93      6    RETURN
 94      6    SWITCH
 95      7    PROGRAM
```

**Notes for implementers:**
- Entries 3/33 ("tion") and 4/36 ("ment") etc. are intentionally duplicated
  at different indices — the encoder picks the one with the cheapest token.
- Entry 70 is "The " (with trailing space, 4 bytes)
- Entry 77 is "ould " (with trailing space, 5 bytes)
- Entries 0, 78 are both `": "` — duplicate by design
- When porting, copy the byte values exactly, not the string interpretation

---

## 4. Decompressor

The decompressor is the **minimum viable port**. If you only implement the
decoder, you can still decompress data produced by the C encoder (the most
common cross-platform scenario: embedded device compresses, server decompresses).

### 4.1 Stream-level decompressor

```
function decompress(input: bytes) → bytes:
    output = []
    history = empty byte buffer (max HISTORY_SIZE = 504)
    pos = 0
    
    while pos < len(input):
        # Read 4-byte block header (little-endian)
        raw_len  = input[pos] | (input[pos+1] << 8)
        comp_len = input[pos+2] | (input[pos+3] << 8)
        pos += 4
        
        if comp_len == 0:
            # Raw block — copy verbatim
            block_output = input[pos : pos + raw_len]
            pos += raw_len
        else:
            # Compressed block — decode tokens
            compressed = input[pos : pos + comp_len]
            block_output = decompress_block(history, compressed, raw_len)
            pos += comp_len
        
        output.append(block_output)
        update_history(history, block_output)
    
    return concatenate(output)
```

### 4.2 Block-level decompressor (the core)

```
function decompress_block(history, compressed, expected_len) → bytes:
    out = byte buffer of size expected_len
    ip = 0          # input position (in compressed)
    op = 0          # output position (in out)
    last_offset = 0 # for repeat-offset tokens
    
    while ip < len(compressed):
        token = compressed[ip++]
        
        if token < 0x40:
            # SHORT LITERAL: copy N raw bytes
            N = (token & 0x3F) + 1
            copy compressed[ip .. ip+N] → out[op .. op+N]
            ip += N
            op += N
        
        else if token < 0x80:
            # DICTIONARY REF (entries 0..63)
            index = token & 0x3F
            entry = DICTIONARY[index]
            copy entry → out[op..]
            op += len(entry)
        
        else if token < 0xC0:
            # SHORT LZ MATCH
            match_len = ((token >> 1) & 0x1F) + 2
            offset = ((token & 0x01) << 8) | compressed[ip++]
            assert offset > 0
            copy_match(out, op, history, offset, match_len)
            op += match_len
            last_offset = offset       # ← remember for repeat token
        
        else if token < 0xD0:
            # REPEAT-OFFSET MATCH
            match_len = (token & 0x0F) + 2
            assert last_offset > 0
            copy_match(out, op, history, last_offset, match_len)
            op += match_len
            # last_offset stays the same — do NOT update it
        
        else if token < 0xE0:
            # DICTIONARY REF (entries 80..95)
            index = 80 + (token & 0x0F)
            entry = DICTIONARY[index]
            copy entry → out[op..]
            op += len(entry)
        
        else if token < 0xF0:
            # DICTIONARY REF (entries 64..79)
            index = 64 + (token & 0x0F)
            entry = DICTIONARY[index]
            copy entry → out[op..]
            op += len(entry)
        
        else:
            # LONG LZ MATCH
            match_len = (token & 0x0F) + 2
            offset = (compressed[ip] << 8) | compressed[ip+1]   # big-endian!
            ip += 2
            assert offset > 0
            copy_match(out, op, history, offset, match_len)
            op += match_len
            last_offset = offset       # ← remember for repeat token
    
    assert op == expected_len
    return out
```

### 4.3 Match copy (with cross-block history)

This is the trickiest part of the decoder. An LZ match offset may point:
1. **Into the current block's output** (offset ≤ op) — normal case
2. **Into the history buffer** (offset > op) — cross-block reference
3. **Spanning both** — starts in history, continues into current output

```
function copy_match(out, op, history, offset, length):
    if offset <= op:
        # Entirely within current block
        src = op - offset
        for i in 0..length-1:
            out[op + i] = out[src + i]    # byte-by-byte! (may overlap)
    else:
        # Starts in history
        hist_back = offset - op           # how far back into history
        hist_start = len(history) - hist_back
        for i in 0..length-1:
            src = hist_start + i
            if src < len(history):
                out[op + i] = history[src]
            else:
                out[op + i] = out[src - len(history)]
```

**CRITICAL: byte-by-byte copy.** You cannot use `memcpy` or array slice copy
for LZ matches because when `offset < length`, the source overlaps with the
destination. Each byte may depend on a previously-written byte in the same
match. This is how LZ achieves run-length encoding of repeated patterns.

### 4.4 History update

After decompressing each block, append the block output to the history buffer.
If the history exceeds `HISTORY_SIZE` (default 504), keep only the last
`HISTORY_SIZE` bytes.

```
function update_history(history, block_data):
    if len(block_data) >= HISTORY_SIZE:
        history = block_data[len(block_data) - HISTORY_SIZE :]
    else if len(history) + len(block_data) <= HISTORY_SIZE:
        history.append(block_data)
    else:
        keep = HISTORY_SIZE - len(block_data)
        if keep > len(history): keep = len(history)
        history = history[len(history) - keep :] + block_data
```

---

## 5. Compressor

The compressor is more complex but follows a clear pipeline. For each block:

### 5.1 High-level flow

```
┌─────────────────────────────────────────────────────┐
│  For each block (up to BLOCK_SIZE = 508 bytes):     │
│                                                     │
│  1. Construct virtual buffer: [history | block]     │
│  2. Seed hash table from history region             │
│  3. Boundary-boost: re-insert last 64 history pos   │
│  4. Scan forward through the block:                 │
│     a. Try repeat-offset cache (3 entries)          │
│     b. Try dictionary (96 entries, first-byte filter)│
│     c. Try LZ hash chain (depth entries per bucket) │
│     d. Pick best by net savings                     │
│     e. Apply lazy match evaluation                  │
│     f. Emit token or accumulate literal             │
│     g. Update hash chain + repeat-offset cache      │
│  5. Flush remaining literals                        │
│  6. If compressed ≥ raw, store raw (comp_len = 0)   │
│  7. Write 4-byte header + payload                   │
│  8. Update history with this block's raw data       │
└─────────────────────────────────────────────────────┘
```

### 5.2 Hash function

```
function hash3(data, pos) → uint16:
    v = data[pos] * 251 + data[pos+1] * 11 + data[pos+2] * 3
    return v & (HASH_SIZE - 1)
```

Where `HASH_SIZE = 2^HASH_BITS` (default `2^9 = 512`).

### 5.3 Hash table structure

The hash table is a 2D array: `head[CHAIN_DEPTH][HASH_SIZE]` of int16.
- Each entry stores a **position** in the virtual buffer.
- Initialize all entries to -1 (no match).
- When inserting position `pos` at hash `h`:
  - Shift: `head[d][h] = head[d-1][h]` for d = DEPTH-1 down to 1
  - Insert: `head[0][h] = pos`

### 5.4 Match finding (pc_find_best)

At each position, try three match sources in order:

**1. Repeat-offset cache** (3 recent offsets, cheapest first):
- Cache slot 0 can use the 1-byte repeat-offset token (cheapest)
- Slots 1-2 are scored as normal LZ (2 or 3 byte token)
- Early reject: check first byte match before full comparison
- `savings = match_len − token_cost`

**2. Dictionary** (96 entries):
- First-byte filter: skip entries whose first byte ≠ current byte
- Full byte comparison only for entries that pass the filter
- `savings = entry_len − 1` (1-byte token)

**3. LZ hash chain** (CHAIN_DEPTH entries per hash):
- Hash the current 3 bytes, look up chain entries
- For each entry: early-reject on first byte, then full comparison
- Short offset (≤511): 2-byte token, `savings = match_len − 2`
- Long offset (>511): 3-byte token, `savings = match_len − 3`
- Prefer nearer offsets at equal savings

**Good-enough threshold:** If any match reaches 8+ bytes, stop searching.

### 5.5 Lazy match evaluation

Before emitting a match at position `i`, check if position `i+1`
(and optionally `i+2` with `LAZY_STEPS=2`) has a strictly better match.
If so, emit position `i` as a literal and use the better match.

Only applies when the current match is below the good-enough threshold (8).

### 5.6 Token emission

```
function emit_literal_run(literals, out):
    while len(literals) > 0:
        chunk = min(len(literals), 64)
        if chunk <= 64:
            out.append(chunk - 1)              # 0x00..0x3F
        out.append(literals[0:chunk])
        literals = literals[chunk:]

function emit_dict(index, out):
    if index < 64:
        out.append(0x40 | index)
    elif index < 80:
        out.append(0xE0 | (index - 64))
    else:
        out.append(0xD0 | (index - 80))

function emit_lz_match(length, offset, out):
    if offset <= 511:
        # Short LZ (2 bytes)
        len_code = length - 2
        out.append(0x80 | (len_code << 1) | (offset >> 8))
        out.append(offset & 0xFF)
    else:
        # Long LZ (3 bytes, offset big-endian)
        len_code = length - 2
        out.append(0xF0 | len_code)
        out.append(offset >> 8)
        out.append(offset & 0xFF)

function emit_repeat(length, out):
    len_code = length - 2
    out.append(0xC0 | len_code)
```

### 5.7 Repeat-offset cache management

```
# On encoder init:
repeat_cache = [0, 0, 0]

# When a new LZ offset is used (NOT a repeat):
repeat_cache[2] = repeat_cache[1]
repeat_cache[1] = repeat_cache[0]
repeat_cache[0] = new_offset
```

---

## 6. Cross-block history

### Encoder side

Before compressing each block, the encoder:
1. Copies `history` + `block_data` into a virtual buffer
2. Seeds the hash table by scanning positions 0..hist_len−3
3. **Boundary boost**: re-inserts positions `hist_len−64..hist_len−1`
   into hash slot 0 (ensures recent cross-block matches survive)
4. Begins the main scan at position `hist_len` (start of actual block)
5. All LZ offsets are relative to the current virtual-buffer position

### Decoder side

The decoder maintains a history buffer identically. When an LZ offset
exceeds the current output position (`offset > op`), the decoder resolves
the reference against the history buffer (see §4.3).

### Key insight

History is **encoder-only intelligence**. The decoder doesn't need to know
the encoder's hash table size, chain depth, or lazy steps. It just needs
the same history buffer to resolve cross-block LZ references.

---

## 7. Encoder optimisations

These are all **encoder-only**. Omitting any of them produces valid streams
that decompress correctly — you just get worse compression ratios. A minimal
port can skip all of these and just use brute-force search.

| Optimisation | Impact | Complexity |
|---|---|---|
| Repeat-offset cache (3 entries) | +5-15% ratio | Low |
| Dictionary first-byte filter | +speed | Low |
| Early reject (first byte check) | +speed | Low |
| Good-enough threshold (8 bytes) | +speed | Low |
| Lazy match (1-2 steps) | +2-5% ratio | Medium |
| Literal run extension | +1-2% ratio | Medium |
| Boundary-boost history seeding | +1-2% ratio | Medium |
| Long-offset length bonus | +1% ratio | Low |
| Offset scoring (prefer nearer) | +0.5% ratio | Low |

---

## 8. Profiles

All profiles produce **decoder-compatible** streams. Any encoder profile
can produce streams decoded by any decoder build.

| Profile | Block | Hash bits | Chain | History | Lazy | Enc RAM | Dec RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| Micro | 192 | 8 | 1 | 64 | 0 | ~0.8K | ~0.5K |
| Minimal | 508 | 8 | 1 | 128 | 1 | ~1.2K | ~1.1K |
| Balanced | 508 | 9 | 2 | 504 | 1 | ~3.1K | ~1.5K |
| Q3 | 508 | 10 | 2 | 1024 | 2 | ~5.6K | ~2.0K |
| Q4 | 508 | 11 | 2 | 2048 | 2 | ~10.6K | ~3.0K |

The decoder RAM is: `block_size + history_size + block_size + overhead`.

---

## 9. Verification test vectors

Use these to verify your implementation produces byte-identical output:

### Test 1: Empty input
```
Input:  (0 bytes)
Output: (0 bytes)
```

### Test 2: Single byte
```
Input:  [0x41]  ("A")
Output: [0x01, 0x00, 0x01, 0x00, 0x00, 0x41]
         ─── header ──  ─── compressed ───
  raw_len=1, comp_len=1, short literal (len 1) + byte 'A'
```

### Test 3: Repeated byte
```
Input:  [0x41] * 8  ("AAAAAAAA")
Expect: compressed smaller than 8+4=12 bytes
Expect: decompresses back to "AAAAAAAA"
```

### Test 4: Roundtrip verification
For any input, `decompress(compress(input)) == input` MUST hold.

Test with:
- Empty input
- 1 byte
- 508 bytes of JSON
- 4096 bytes of repeated prose (multi-block)
- 508 bytes of random data (should store raw, comp_len=0)

---

## 10. Common porting pitfalls

### Byte-by-byte LZ copy
The most common bug. When `offset < match_length`, the copy overlaps.
You MUST NOT use bulk copy (memcpy, arraycopy, slice assignment).
Copy one byte at a time in a forward loop.

### Signed vs unsigned bytes
Java bytes are signed (−128..127). Use `& 0xFF` when interpreting as unsigned.
Python bytes are unsigned (0..255) — no issue.
C# bytes are unsigned — no issue.

### Little-endian header, big-endian long offset
The block header is little-endian (low byte first).
The long LZ offset (0xF0..0xFF token) is big-endian (high byte first).
Do NOT mix these up.

### Dictionary must be byte-exact
Entry 0 is `[0x22, 0x3A, 0x20, 0x22]`, not the string `": "`.
Several entries contain non-printable bytes (0x0A, 0x0D).
Copy from the hex values, not the string representations.

### Repeat-offset is NOT updated by repeat tokens
When a repeat-offset token (0xC0..0xCF) fires, `last_offset` stays unchanged.
Only short LZ (0x80..0xBF) and long LZ (0xF0..0xFF) update `last_offset`.

### History size must match
The encoder and decoder must use the same `HISTORY_SIZE`. The default is 504.
If you produce a stream with history=2048 (Q4), the decoder must also use
history=2048 to resolve cross-block references correctly.

### Raw fallback
When `comp_len == 0` in the header, the payload is raw — do NOT try to
decode tokens from it.

### Block size vs input size
The last block may be shorter than `BLOCK_SIZE`. The `raw_len` in the
header tells you the actual size. Do not assume all blocks are 508 bytes.
