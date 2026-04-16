# picocompress — Algorithm description

## Overview

picocompress is a block-based LZ-class compressor designed for embedded
systems with RAM budgets from 1 KB to 15 KB. It achieves competitive
compression ratios on short structured payloads (JSON, CSV, telemetry,
fixed-width records) by combining six techniques in a single forward pass:

1. **LZ sliding-window matching** with a compact hash chain
2. **Repeat-offset coding** (1-byte token for recurring match distances)
3. **Static dictionary** (64 entries, ROM-resident, 1-byte token each)
4. **Cross-block history** (soft chaining — encoder-side only intelligence)
5. **Lazy match evaluation** (configurable 1- or 2-step lookahead)
6. **Long-offset LZ** (3-byte token for cross-block matches >511 bytes)

The decoder is single-pass, bounded, and requires no hash tables or
search structures. It processes tokens sequentially and resolves
cross-block references using a small history tail buffer.

## Token format (v3)

Every compressed block is a sequence of tokens. The high bits of each
token byte determine its type:

```
Byte range   Type              Size    Description
──────────   ────              ────    ───────────
0x00..0x3F   Short literal     1+N     Emit N raw bytes (N = 1..64)
0x40..0x7F   Dictionary ref    1       Emit predefined entry (index 0..63)
0x80..0xBF   LZ match          2       5-bit length + 9-bit offset (short)
0xC0..0xDF   Repeat-offset     1       5-bit length, reuse last LZ offset
0xE0..0xEF   Extended literal  1+N     Emit N raw bytes (N = 65..80)
0xF0..0xFF   Long-offset LZ    3       4-bit length + 16-bit offset
```

### Short LZ match (0x80..0xBF)

```
Byte 0: 1 0 L4 L3 L2 L1 L0 O8
Byte 1: O7 O6 O5 O4 O3 O2 O1 O0

match_length = L[4:0] + PC_MATCH_MIN    (range: 2..33)
offset       = {O8, O[7:0]}             (range: 1..511)
```

### Repeat-offset match (0xC0..0xDF)

```
Byte 0: 1 1 0 L4 L3 L2 L1 L0

match_length = L[4:0] + PC_MATCH_MIN    (range: 2..33)
offset       = (reuse previous LZ offset)
```

This saves 1 byte per match when the same offset recurs — extremely
common in structured data with fixed-stride fields (e.g. timestamp
bytes repeating every 12 bytes in a packed record array).

### Long-offset LZ match (0xF0..0xFF)

```
Byte 0: 1 1 1 1 L3 L2 L1 L0
Byte 1: O15 O14 O13 O12 O11 O10 O9 O8
Byte 2: O7 O6 O5 O4 O3 O2 O1 O0

match_length = L[3:0] + PC_LONG_MATCH_MIN   (range: 2..17)
offset       = {O[15:0]}                    (range: 1..65535)
```

This 3-byte token enables cross-block matches beyond 511 bytes.
Short-offset matches (≤511) still use the cheaper 2-byte token.
Long-offset tokens only fire when history is configured >504 bytes
(Q3/Q4 profiles). The encoder selects the cheapest token
automatically based on the match offset.

### Dictionary reference (0x40..0x7F)

```
Byte 0: 0 1 I5 I4 I3 I2 I1 I0

index = I[5:0]    (range: 0..63)
emit  = pc_static_dict[index]  (1..8 bytes from ROM)
```

The 64-entry dictionary is ROM-resident and covers:
- JSON structural tokens: `{`, `}`, `:`, `,`, `":"`, `,"`, `":{`, `":[`
- Common bigrams: `\r\n`, `id`, `00`, double-space
- English suffixes: `the`, `ing`, `ion`, `ent`, `ter`, `and`, `tion`, `ment`, `ation`
- Data keywords: `true`, `false`, `null`, `name`, `data`, `time`, `type`, `mode`,
  `code`, `size`, `list`, `item`, `text`, `no":`, `number":`
- Longer patterns: `error`, `value`, `state`, `alert`, `input`, `order`,
  `status`, `number`, `active`, `device`, `region`, `string`, `result`, `length`,
  `message`, `content`, `request`, `default`, `operator`, `response`, `https://`

Single-byte symbols (`{`, `}`, `:`, `,`) have a special savings bonus
that lets them fire even between matches when they would otherwise be
absorbed into a literal run.

## Block framing

Each block is framed with a 4-byte header:

```
Bytes 0-1: raw_len   (little-endian uint16, original size)
Bytes 2-3: comp_len  (little-endian uint16, compressed size; 0 = stored raw)
```

If compression does not reduce size, `comp_len` is set to 0 and the
raw block data follows the header verbatim (raw fallback).

## Compression pipeline

For each block (default 508 bytes):

```
┌──────────────────────────────────────────────────┐
│  Input position i                                │
│                                                  │
│  1. Try repeat-offset cache (3 recent offsets)   │
│     → first-byte + second-byte early reject      │
│     → slot 0 = cheap 1-byte repeat token         │
│     → slots 1-2 = scored as normal LZ            │
│     → bail if match >= GOOD_MATCH (8)            │
│                                                  │
│  2. Try static dictionary (64 entries)           │
│     → first-byte filter, then memcmp             │
│     → savings = entry_len - 1  (1-byte token)    │
│     → bail if match >= GOOD_MATCH                │
│                                                  │
│  3. Try LZ hash chain (depth=2, 9-bit hash)      │
│     → hash3(vbuf + i) → head[d][hash]            │
│     → first-byte early reject before compare     │
│     → short offset (≤511): 2-byte token          │
│     → long offset (>511): 3-byte token           │
│     → offset scoring: prefer nearer at equal len │
│     → long-offset bonus: +2 bytes wins over near │
│     → bail if match >= GOOD_MATCH                │
│                                                  │
│  4. Pick best savings across repeat/dict/LZ      │
│                                                  │
│  5. Literal run extension: skip savings≤1 mid-run│
│                                                  │
│  6. Lazy evaluation: only if match < GOOD_MATCH  │
│     → check (i+1)..(i+LAZY_STEPS) for better    │
│     → skip current position if found             │
│                                                  │
│  7. Emit: dict ref / repeat-offset /             │
│     short-offset LZ / long-offset LZ / literal   │
│                                                  │
│  8. Update repeat-offset cache + hash chain       │
└──────────────────────────────────────────────────┘
```

## Cross-block history (soft chaining)

The encoder maintains a 504-byte history buffer (configurable) containing
the tail of the previously compressed block. Before compressing a new block, it
constructs a virtual buffer `[history | block]` and seeds the hash
table from the history region. This allows matches that span the
block boundary — the key to compressing multi-block payloads
effectively.

The decoder maintains an identical 128-byte history buffer. When an LZ
match offset exceeds the current output position, the decoder resolves
it against the history (previous block tail). This adds zero search
cost to decoding — it's just a different source pointer for the copy.

**Critically**: no extra decode passes, no hash tables, no search
structures on the decode side. The decoder remains single-pass and
bounded.

## Performance tricks

### Hash function

```c
uint32_t v = p[0]*251 + p[1]*11 + p[2]*3;
hash = v & (HASH_SIZE - 1);
```

Three prime-weighted bytes give excellent distribution for short
matches. The constants are chosen to avoid expensive multiply
instructions on ARM Cortex-M (small constants → shift+add chains).

### Hash chain with bounded depth

The hash table uses `PC_HASH_CHAIN_DEPTH` (default 2) entries per
hash bucket stored as parallel arrays. This gives collision handling
without linked lists or pointer chasing — just two flat array lookups
per position. At depth 2 with 512 buckets, the entire hash table is
2 KB on the stack.

### Lazy match evaluation

Before emitting a match at position `i`, the encoder checks whether
position `i+1` has a strictly better match. If so, it defers — this
consistently improves ratio by 2–5% with negligible speed cost (one
extra hash lookup per match site).

### First-byte filter for dictionary

The dictionary scan checks `data[0]` against each entry's first byte
before calling `memcmp`. On typical data, this eliminates >90% of
dictionary entries without touching memory, making the 64-entry scan
effectively free.

### Raw fallback

If compression does not reduce block size (common for random/
encrypted/already-compressed data), the block is stored verbatim with
only the 4-byte frame header. Expansion is bounded to +4 bytes per
block worst case.

### Repeat-offset cache

The encoder maintains a 3-entry cache of recent LZ offsets. At each
position, these are probed **before** the hash chain — with first-byte
and second-byte early rejection. Only cache slot 0 (most recent) can
emit the cheap 1-byte repeat-offset token; slots 1 and 2 are scored
as normal LZ matches. This finds matches on recurring struct strides
without any hash lookup at all.

### Repeat-offset detection

When the best LZ match uses a new offset, it is pushed into the cache
(shifting slots 1→2, 0→1). The decoder tracks only the most recent
offset for the repeat-offset token — so the cache is encoder-only
intelligence with zero decoder cost.

### Good-enough threshold

Once any match of 8+ bytes is found (repeat, dictionary, or LZ), the
encoder stops probing immediately. Long matches are almost never
beaten by further search — this saves significant CPU on repetitive
data while preserving ratio.

### Conditional lazy evaluation

Lazy matching only fires when the current match is shorter than the
good-enough threshold (8 bytes). Long matches are accepted immediately.
This saves wasted probes on already-optimal matches while still
improving short-match decisions by 2–5%.

### Literal run extension

Weak matches (savings ≤ 1 byte) are suppressed when the encoder is
mid-literal-run. Breaking a literal run for a tiny match costs a
literal-header byte that often negates the match savings.

### Long-offset length bonus

When a long-offset match (>511 bytes, 3-byte token) is 2+ bytes
longer than the best short-offset match, it is preferred despite the
extra token byte. This catches cross-block references that would
otherwise be discarded.

### Early reject

Both the repeat-cache probe and LZ hash-chain probe check the first
byte at the candidate position before calling `pc_match_len`. This
eliminates the majority of non-matches without entering the compare
loop.

### Offset scoring

At equal savings, the encoder prefers matches with shorter offsets —
these are more likely to use the cheaper 2-byte short-offset token
and keep the repeat-cache hot.

### Boundary-boost history seeding

After seeding the hash table from history, the encoder re-injects
the last 64 history positions into hash slot 0. This ensures "just
out of block" matches — the highest-value cross-block references —
survive the full block scan instead of being buried by earlier
history entries.

### Hash function validation

Six candidate hash functions were benchmarked (FNV-1a, shift-XOR,
multiplicative, DJB2, MurmurHash3, and the current weighted-sum).
The current hash (`a*251 + b*11 + c*3`) won on compression ratio
and is near-fastest. See `src/hash_spike.c` for the full analysis.

## Memory budget

With default configuration (`PC_BLOCK_SIZE=508`, `PC_HASH_BITS=9`,
`PC_HASH_CHAIN_DEPTH=2`, `PC_HISTORY_SIZE=504`):

| Component | Encode | Decode |
|---|---:|---:|
| Block buffer | 508 B | 508 B |
| History buffer | 504 B | 504 B |
| Hash table (stack) | 2048 B | — |
| Combined virtual buffer (stack) | 1012 B | — |
| Compressed scratch (stack) | 528 B | — |
| Payload buffer | — | 508 B |
| Static dictionary (ROM) | ~350 B | ~350 B |
| **Total RAM** | **~4.6 KB** | **~1.5 KB** |

The hash configuration is encoder-only. Changing `PC_HASH_BITS` or
`PC_HASH_CHAIN_DEPTH` does not affect the compressed format — any
decoder build can decompress streams from any encoder configuration.

## Profiles

All encoder parameters are `#ifndef`-guarded compile-time constants.
The decoder is completely independent of these — it only reads the
token stream. **Any encoder profile produces streams that any decoder
can decompress.**

| Profile | Block | Hash | Depth | History | Lazy | Enc RAM | Dec RAM |
|---|---:|---|---:|---:|---:|---:|---:|
| Micro | 192 | 256×1 | 1 | 64 | 1 | ~1.0 KB | ~0.5 KB |
| Minimal | 508 | 256×1 | 1 | 128 | 1 | ~1.8 KB | ~0.7 KB |
| Balanced | 508 | 512×2 | 2 | 504 | 1 | ~4.6 KB | ~1.5 KB |
| Aggressive | 508 | 256×4 | 4 | 504 | 1 | ~4.6 KB | ~1.5 KB |
| Q3 | 508 | 1024×2 | 2 | 1024 | 2 | ~7.7 KB | ~2.0 KB |
| Q4 | 508 | 2048×2 | 2 | 2048 | 2 | ~13.8 KB | ~3.0 KB |

### Design principles behind the profiles

- **Block size 508 is optimal** for all profiles with ≥2 KB encode
  budget. Smaller blocks (256, 384) reduce intra-block match reach
  more than the freed RAM can compensate via better hash tables.
  Block size only drops for the Micro profile where the entire
  budget is ~1 KB.

- **History dominates at larger budgets.** Cross-block reach
  (history size) gives the biggest ratio improvement per byte of
  RAM invested. The Q3→Q4 jump (1024→2048 history) improves prose
  compression by +10% for just +6 KB RAM.

- **Depth beats width for hash tables** at the same byte budget,
  because the block is small (508 positions) and collisions are
  frequent — deeper chains find the best match among them.

- **Lazy steps: 2-step is only worthwhile with large history**
  (Q3/Q4) where the search space is bigger and deferring a match
  by one position more often finds a longer cross-block reference.
