//! picocompress — Pure Rust port of the picocompress block-based LZ compressor.
//!
//! Produces byte-identical output to the C reference implementation.
//!
//! # Examples
//! ```
//! let data = b"hello world hello world hello world";
//! let compressed = picocompress::compress(data);
//! let decompressed = picocompress::decompress(&compressed).unwrap();
//! assert_eq!(&decompressed, data);
//! ```

#![no_std]

extern crate alloc;

use alloc::vec;
use alloc::vec::Vec;
use core::fmt;

// ---- Constants (must match C exactly) ----

const BLOCK_SIZE: u16 = 508;
const LITERAL_MAX: u16 = 64;
const MATCH_MIN: u16 = 2;
const MATCH_CODE_BITS: u16 = 5;
const MATCH_MAX: u16 = MATCH_MIN + ((1 << MATCH_CODE_BITS) - 1); // 33
const OFFSET_SHORT_BITS: u16 = 9;
const OFFSET_SHORT_MAX: u16 = (1 << OFFSET_SHORT_BITS) - 1; // 511
const LONG_MATCH_MIN: u16 = 2;
const LONG_MATCH_MAX: u16 = 17;
const OFFSET_LONG_MAX: u16 = 65535;
const DICT_COUNT: usize = 96;
const HASH_BITS: u32 = 9;
const HASH_CHAIN_DEPTH: usize = 2;
const HISTORY_SIZE: u16 = 504;
const GOOD_MATCH: u16 = 8;
const REPEAT_CACHE_SIZE: usize = 3;
const LAZY_STEPS: u16 = 1;
const BLOCK_MAX_COMPRESSED: usize =
    BLOCK_SIZE as usize + (BLOCK_SIZE as usize / LITERAL_MAX as usize) + 16;

// ---- Static dictionary (96 entries, identical to C) ----

struct DictEntry {
    data: &'static [u8],
}

static STATIC_DICT: [DictEntry; DICT_COUNT] = [
    // 0-3: 4-5B multi-byte patterns
    DictEntry { data: b"\":\x20\"" },           // 0: ": "
    DictEntry { data: b"},\n\"" },              // 1: },\n"
    DictEntry { data: b"</div" },               // 2
    DictEntry { data: b"tion" },                // 3
    // 4-7: common English suffixes
    DictEntry { data: b"ment" },                // 4
    DictEntry { data: b"ness" },                // 5
    DictEntry { data: b"able" },                // 6
    DictEntry { data: b"ight" },                // 7
    // 8-15: three-byte patterns
    DictEntry { data: b"\":\"" },               // 8: ":"
    DictEntry { data: b"</di" },                // 9
    DictEntry { data: b"=\"ht" },               // 10
    DictEntry { data: b"the" },                 // 11
    DictEntry { data: b"ing" },                 // 12
    DictEntry { data: b",\","},                 // 13: ","
    DictEntry { data: b"\":{" },                // 14
    DictEntry { data: b"\":[" },                // 15
    // 16-23: more three-byte
    DictEntry { data: b"ion" },                 // 16
    DictEntry { data: b"ent" },                 // 17
    DictEntry { data: b"ter" },                 // 18
    DictEntry { data: b"and" },                 // 19
    DictEntry { data: b"/>\r\n" },              // 20
    DictEntry { data: b"\"}," },                // 21
    DictEntry { data: b"\"]," },                // 22
    DictEntry { data: b"have" },                // 23
    // 24-39: four-byte
    DictEntry { data: b"no\":" },               // 24
    DictEntry { data: b"true" },                // 25
    DictEntry { data: b"null" },                // 26
    DictEntry { data: b"name" },                // 27
    DictEntry { data: b"data" },                // 28
    DictEntry { data: b"time" },                // 29
    DictEntry { data: b"type" },                // 30
    DictEntry { data: b"mode" },                // 31
    DictEntry { data: b"http" },                // 32
    DictEntry { data: b"tion" },                // 33
    DictEntry { data: b"code" },                // 34
    DictEntry { data: b"size" },                // 35
    DictEntry { data: b"ment" },                // 36
    DictEntry { data: b"list" },                // 37
    DictEntry { data: b"item" },                // 38
    DictEntry { data: b"text" },                // 39
    // 40-47: five-byte
    DictEntry { data: b"false" },               // 40
    DictEntry { data: b"error" },               // 41
    DictEntry { data: b"value" },               // 42
    DictEntry { data: b"state" },               // 43
    DictEntry { data: b"alert" },               // 44
    DictEntry { data: b"input" },               // 45
    DictEntry { data: b"ation" },               // 46
    DictEntry { data: b"order" },               // 47
    // 48-55: six-byte
    DictEntry { data: b"status" },              // 48
    DictEntry { data: b"number" },              // 49
    DictEntry { data: b"active" },              // 50
    DictEntry { data: b"device" },              // 51
    DictEntry { data: b"region" },              // 52
    DictEntry { data: b"string" },              // 53
    DictEntry { data: b"result" },              // 54
    DictEntry { data: b"length" },              // 55
    // 56-59: seven-byte
    DictEntry { data: b"message" },             // 56
    DictEntry { data: b"content" },             // 57
    DictEntry { data: b"request" },             // 58
    DictEntry { data: b"default" },             // 59
    // 60-63: eight-byte
    DictEntry { data: b"number\":" },           // 60
    DictEntry { data: b"operator" },            // 61
    DictEntry { data: b"https://" },            // 62
    DictEntry { data: b"response" },            // 63
    // 64-67: sentence starters
    DictEntry { data: b". The " },              // 64
    DictEntry { data: b". It " },               // 65
    DictEntry { data: b". This " },             // 66
    DictEntry { data: b". A " },                // 67
    // 68-71: capitalized terms
    DictEntry { data: b"HTTP" },                // 68
    DictEntry { data: b"JSON" },                // 69
    DictEntry { data: b"The " },                // 70
    DictEntry { data: b"None" },                // 71
    // 72-75: phoneme
    DictEntry { data: b"ment" },                // 72
    DictEntry { data: b"ness" },                // 73
    DictEntry { data: b"able" },                // 74
    DictEntry { data: b"ight" },                // 75
    // 76-79: phoneme + structural
    DictEntry { data: b"ation" },               // 76
    DictEntry { data: b"ould " },               // 77
    DictEntry { data: b"\":\x20\"" },           // 78: ": "
    DictEntry { data: b"\",\x20\"" },           // 79: ", "
    // 80-95: uppercase keywords (0xD0..0xDF tokens)
    DictEntry { data: b"DIM" },                 // 80
    DictEntry { data: b"FOR" },                 // 81
    DictEntry { data: b"END" },                 // 82
    DictEntry { data: b"REL" },                 // 83
    DictEntry { data: b"EACH" },                // 84
    DictEntry { data: b"LOAD" },                // 85
    DictEntry { data: b"SAVE" },                // 86
    DictEntry { data: b"CARD" },                // 87
    DictEntry { data: b"JUMP" },                // 88
    DictEntry { data: b"PRINT" },               // 89
    DictEntry { data: b"INPUT" },               // 90
    DictEntry { data: b"GOSUB" },               // 91
    DictEntry { data: b"STREAM" },              // 92
    DictEntry { data: b"RETURN" },              // 93
    DictEntry { data: b"SWITCH" },              // 94
    DictEntry { data: b"PROGRAM" },             // 95
];

// ---- Error type ----

/// Decompression error.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// Compressed data is corrupt or truncated.
    Corrupt,
    /// Input data is invalid (e.g. empty when not expected).
    InvalidInput,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Corrupt => f.write_str("corrupt compressed data"),
            Error::InvalidInput => f.write_str("invalid input"),
        }
    }
}

// ---- Options / builder ----

/// Compression options with builder pattern.
#[derive(Debug, Clone, Copy)]
pub struct Options {
    pub block_size: u16,
    pub history_size: u16,
    pub hash_bits: u32,
    pub hash_chain_depth: usize,
    pub lazy_steps: u16,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            block_size: BLOCK_SIZE,
            history_size: HISTORY_SIZE,
            hash_bits: HASH_BITS,
            hash_chain_depth: HASH_CHAIN_DEPTH,
            lazy_steps: LAZY_STEPS,
        }
    }
}

impl Options {
    /// Create new default options (Balanced profile).
    pub fn new() -> Self {
        Self::default()
    }

    /// Set the block size (max 511).
    pub fn block_size(mut self, size: u16) -> Self {
        self.block_size = size;
        self
    }

    /// Set the history buffer size for cross-block matching.
    pub fn history_size(mut self, size: u16) -> Self {
        self.history_size = size;
        self
    }

    /// Set the hash table size as log2 (e.g. 9 = 512 buckets).
    pub fn hash_bits(mut self, bits: u32) -> Self {
        self.hash_bits = bits;
        self
    }

    /// Set the hash chain depth (entries per bucket).
    pub fn hash_chain_depth(mut self, depth: usize) -> Self {
        self.hash_chain_depth = depth;
        self
    }

    /// Set the number of lazy evaluation steps.
    pub fn lazy_steps(mut self, steps: u16) -> Self {
        self.lazy_steps = steps;
        self
    }
}

// ---- Public API ----

/// Compress input bytes with default options.
pub fn compress(input: &[u8]) -> Vec<u8> {
    compress_with(input, &Options::default())
}

/// Compress input bytes with custom options.
pub fn compress_with(input: &[u8], opts: &Options) -> Vec<u8> {
    let mut encoder = Encoder::new(opts);
    let mut output = Vec::new();
    encoder.sink(input, &mut output);
    encoder.finish(&mut output);
    output
}

/// Decompress picocompress data. Returns the original bytes or an error.
pub fn decompress(compressed: &[u8]) -> Result<Vec<u8>, Error> {
    let mut decoder = Decoder::new();
    let mut output = Vec::new();
    decoder.sink(compressed, &mut output)?;
    decoder.finish()?;
    Ok(output)
}

// ---- Hash function (portable, matches C) ----

fn hash3(p: &[u8], mask: u16) -> u16 {
    let v = (p[0] as u32).wrapping_mul(251)
        + (p[1] as u32).wrapping_mul(11)
        + (p[2] as u32).wrapping_mul(3);
    (v as u16) & mask
}

// ---- Match length ----

fn match_len(a: &[u8], b: &[u8], limit: u16) -> u16 {
    let mut m: u16 = 0;
    let lim = limit;
    while m < lim && a[m as usize] == b[m as usize] {
        m += 1;
    }
    m
}

// ---- Emit literals ----

fn emit_literals(src: &[u8], dst: &mut [u8], op: &mut u16) -> bool {
    let src_len = src.len() as u16;
    let dst_cap = dst.len() as u16;
    let mut pos: u16 = 0;
    while pos < src_len {
        let mut chunk = src_len - pos;
        if chunk > LITERAL_MAX {
            chunk = LITERAL_MAX;
        }
        if (*op as u32) + 1 + (chunk as u32) > dst_cap as u32 {
            return false;
        }
        dst[*op as usize] = (chunk - 1) as u8; // 0x00..0x3F
        *op += 1;
        dst[(*op as usize)..(*op as usize + chunk as usize)]
            .copy_from_slice(&src[pos as usize..(pos + chunk) as usize]);
        *op += chunk;
        pos += chunk;
    }
    true
}

// ---- Hash table insert ----

fn head_insert(head: &mut [Vec<i16>], hash: u16, pos: i16, depth: usize) {
    let h = hash as usize;
    for d in (1..depth).rev() {
        head[d][h] = head[d - 1][h];
    }
    head[0][h] = pos;
}

// ---- Find best match ----

#[derive(Clone, Copy)]
struct MatchResult {
    len: u16,
    off: u16,
    dict: u16, // u16::MAX means no dict
    is_repeat: bool,
}

impl MatchResult {
    fn none() -> Self {
        Self {
            len: 0,
            off: 0,
            dict: u16::MAX,
            is_repeat: false,
        }
    }
}

fn find_best(
    vbuf: &[u8],
    vpos: u16,
    head: &[Vec<i16>],
    rep_offsets: &[u16; REPEAT_CACHE_SIZE],
    good_match: u16,
    skip_dict: bool,
    depth: usize,
    hash_mask: u16,
) -> (i32, MatchResult) {
    let vbuf_len = vbuf.len() as u16;
    let remaining = vbuf_len - vpos;
    let mut best_savings: i32 = 0;
    let mut result = MatchResult::none();

    // 1. Repeat-offset cache
    if remaining >= MATCH_MIN {
        let max_rep = if remaining > MATCH_MAX {
            MATCH_MAX
        } else {
            remaining
        };
        for d in 0..REPEAT_CACHE_SIZE {
            let off = rep_offsets[d];
            if off == 0 || off > vpos {
                continue;
            }
            // early reject: first byte
            if vbuf[vpos as usize] != vbuf[(vpos - off) as usize] {
                continue;
            }
            // second byte check
            if remaining >= 2 && vbuf[(vpos + 1) as usize] != vbuf[(vpos - off + 1) as usize] {
                continue;
            }
            let len = match_len(
                &vbuf[(vpos - off) as usize..],
                &vbuf[vpos as usize..],
                max_rep,
            );
            if len < MATCH_MIN {
                continue;
            }

            let is_rep = d == 0 && len <= 17;
            let token_cost: i32 = if is_rep {
                1
            } else if off <= OFFSET_SHORT_MAX {
                2
            } else {
                3
            };
            let s = len as i32 - token_cost;

            if s > best_savings {
                best_savings = s;
                result = MatchResult {
                    len,
                    off,
                    dict: u16::MAX,
                    is_repeat: is_rep,
                };
                if len >= good_match {
                    return (best_savings, result);
                }
            }
        }
    }

    // 2. Dictionary match
    if !skip_dict {
        let first_byte = vbuf[vpos as usize];
        for d in 0..DICT_COUNT {
            let entry = &STATIC_DICT[d];
            let dlen = entry.data.len() as u16;
            if dlen > remaining {
                continue;
            }
            if (dlen as i32 - 1) <= best_savings {
                continue;
            }
            if entry.data[0] != first_byte {
                continue;
            }
            if &vbuf[vpos as usize..(vpos + dlen) as usize] != entry.data {
                continue;
            }
            let s = dlen as i32 - 1;
            best_savings = s;
            result = MatchResult {
                len: dlen,
                off: 0,
                dict: d as u16,
                is_repeat: false,
            };
            if dlen >= good_match {
                return (best_savings, result);
            }
        }
    }

    // 3. LZ hash-chain match
    if remaining >= 3 {
        let hash = hash3(&vbuf[vpos as usize..], hash_mask);
        let max_len_short = if remaining > MATCH_MAX {
            MATCH_MAX
        } else {
            remaining
        };
        let max_len_long = if remaining > LONG_MATCH_MAX {
            LONG_MATCH_MAX
        } else {
            remaining
        };
        let first_byte = vbuf[vpos as usize];

        for d in 0..depth {
            let prev = head[d][hash as usize];
            if prev < 0 {
                continue;
            }
            let prev_pos = prev as u16;
            if prev_pos >= vpos {
                continue;
            }
            let off = vpos - prev_pos;
            if off == 0 || off > OFFSET_LONG_MAX {
                continue;
            }
            // early reject
            if vbuf[prev_pos as usize] != first_byte {
                continue;
            }
            let max_len = if off <= OFFSET_SHORT_MAX {
                max_len_short
            } else {
                max_len_long
            };
            let len = match_len(&vbuf[prev_pos as usize..], &vbuf[vpos as usize..], max_len);
            if len < MATCH_MIN {
                continue;
            }

            let token_cost: i32 = if off <= OFFSET_SHORT_MAX { 2 } else { 3 };
            let s = len as i32 - token_cost;

            if s > best_savings
                || (s == best_savings && len > result.len)
                || (s == best_savings && len == result.len && off < result.off)
                || (s == best_savings - 1 && len >= result.len + 2)
            {
                best_savings = len as i32 - token_cost;
                result = MatchResult {
                    len,
                    off,
                    dict: u16::MAX,
                    is_repeat: false,
                };
                if len >= good_match {
                    return (best_savings, result);
                }
            }
        }
    }

    (best_savings, result)
}

// ---- Block compression ----

fn compress_block(
    vbuf: &[u8],
    hist_len: u16,
    block_len: u16,
    out: &mut [u8],
    depth: usize,
    hash_mask: u16,
    lazy_steps: u16,
) -> u16 {
    let hash_size = (hash_mask as usize) + 1;
    let mut head: Vec<Vec<i16>> = vec![vec![-1i16; hash_size]; depth];
    let mut rep_offsets = [0u16; REPEAT_CACHE_SIZE];
    let vbuf_len = (hist_len + block_len) as u16;
    let mut op: u16 = 0;
    let out_cap = out.len() as u16;

    // Seed hash table from history
    if hist_len >= 3 {
        let mut p: u16 = 0;
        while p + 2 < hist_len {
            head_insert(&mut head, hash3(&vbuf[p as usize..], hash_mask), p as i16, depth);
            p += 1;
        }
        // Re-inject positions near the block boundary into slot 0
        let tail_start = if hist_len > 64 { hist_len - 64 } else { 0 };
        let mut p = tail_start;
        while p + 2 < hist_len {
            let h = hash3(&vbuf[p as usize..], hash_mask);
            if head[0][h as usize] != p as i16 {
                let save = head[depth - 1][h as usize];
                head_insert(&mut head, h, p as i16, depth);
                head[depth - 1][h as usize] = save;
            }
            p += 1;
        }
    }

    let mut anchor = hist_len;
    let mut vpos = hist_len;

    // Self-disabling dictionary check
    let dict_skip = if block_len >= 1 {
        let b0 = vbuf[hist_len as usize];
        if b0 == b'{' || b0 == b'[' || b0 == b'<' || b0 == 0xEF {
            false
        } else {
            let check_len = if block_len < 4 { block_len } else { 4 };
            let mut skip = false;
            for ci in 0..check_len {
                let c = vbuf[(hist_len + ci) as usize];
                if c < 0x20 || c > 0x7E {
                    skip = true;
                    break;
                }
            }
            skip
        }
    } else {
        false
    };

    while vpos < vbuf_len {
        if vbuf_len - vpos < MATCH_MIN {
            break;
        }

        let (mut best_savings, mut best) = find_best(
            &vbuf[..vbuf_len as usize],
            vpos,
            &head,
            &rep_offsets,
            GOOD_MATCH,
            dict_skip,
            depth,
            hash_mask,
        );

        // Insert current position into hash table
        if vbuf_len - vpos >= 3 {
            head_insert(
                &mut head,
                hash3(&vbuf[vpos as usize..], hash_mask),
                vpos as i16,
                depth,
            );
        }

        // Literal run extension: skip weak matches mid-run
        if best_savings <= 1 && best.dict == u16::MAX && anchor < vpos {
            best_savings = 0;
        }

        // Lazy matching
        if best_savings > 0 && best.len < GOOD_MATCH {
            let mut deferred = false;
            for step in 1..=lazy_steps {
                let npos = vpos + step;
                if npos >= vbuf_len || vbuf_len - npos < MATCH_MIN {
                    break;
                }
                let (n_sav, _n_best) = find_best(
                    &vbuf[..vbuf_len as usize],
                    npos,
                    &head,
                    &rep_offsets,
                    GOOD_MATCH,
                    dict_skip,
                    depth,
                    hash_mask,
                );
                if n_sav > best_savings {
                    // Insert skipped positions
                    for s in 0..step {
                        let sp = vpos + s;
                        if vbuf_len - sp >= 3 {
                            head_insert(
                                &mut head,
                                hash3(&vbuf[sp as usize..], hash_mask),
                                sp as i16,
                                depth,
                            );
                        }
                    }
                    vpos = npos;
                    deferred = true;
                    break;
                }
            }
            if deferred {
                continue; // retry at new position (goto retry_pos equivalent)
            }
        }

        // Emit
        if best_savings > 0 {
            let lit_len = vpos - anchor;

            if lit_len > 0 {
                if !emit_literals(
                    &vbuf[anchor as usize..vpos as usize],
                    &mut out[..out_cap as usize],
                    &mut op,
                ) {
                    return u16::MAX;
                }
            }

            if best.dict != u16::MAX {
                if (op as u32) + 1 > out_cap as u32 {
                    return u16::MAX;
                }
                if best.dict < 64 {
                    out[op as usize] = 0x40 | (best.dict as u8 & 0x3F);
                } else if best.dict < 80 {
                    out[op as usize] = 0xE0 | ((best.dict - 64) as u8 & 0x0F);
                } else {
                    out[op as usize] = 0xD0 | ((best.dict - 80) as u8 & 0x0F);
                }
                op += 1;
            } else if best.is_repeat {
                if (op as u32) + 1 > out_cap as u32 {
                    return u16::MAX;
                }
                out[op as usize] = 0xC0 | ((best.len - MATCH_MIN) as u8 & 0x0F);
                op += 1;
            } else if best.off <= OFFSET_SHORT_MAX && best.len <= MATCH_MAX {
                // Short-offset LZ: 2-byte token
                if (op as u32) + 2 > out_cap as u32 {
                    return u16::MAX;
                }
                out[op as usize] = 0x80
                    | (((best.len - MATCH_MIN) as u8 & 0x1F) << 1)
                    | ((best.off >> 8) as u8 & 0x01);
                op += 1;
                out[op as usize] = (best.off & 0xFF) as u8;
                op += 1;
            } else {
                // Long-offset LZ: 3-byte token
                let elen = if best.len > LONG_MATCH_MAX {
                    LONG_MATCH_MAX
                } else {
                    best.len
                };
                if (op as u32) + 3 > out_cap as u32 {
                    return u16::MAX;
                }
                out[op as usize] = 0xF0 | ((elen - LONG_MATCH_MIN) as u8 & 0x0F);
                op += 1;
                out[op as usize] = ((best.off >> 8) & 0xFF) as u8;
                op += 1;
                out[op as usize] = (best.off & 0xFF) as u8;
                op += 1;
                best.len = elen;
            }

            // Update repeat-offset cache
            if !best.is_repeat && best.off != 0 && best.dict == u16::MAX {
                rep_offsets[2] = rep_offsets[1];
                rep_offsets[1] = rep_offsets[0];
                rep_offsets[0] = best.off;
            }

            // Insert matched positions into hash table
            let mut k: u16 = 1;
            while k < best.len && (vpos + k + 2) < vbuf_len {
                head_insert(
                    &mut head,
                    hash3(&vbuf[(vpos + k) as usize..], hash_mask),
                    (vpos + k) as i16,
                    depth,
                );
                k += 1;
            }

            vpos += best.len;
            anchor = vpos;
        } else {
            vpos += 1;
        }
    }

    // Emit trailing literals
    if anchor < vbuf_len {
        if !emit_literals(
            &vbuf[anchor as usize..vbuf_len as usize],
            &mut out[..out_cap as usize],
            &mut op,
        ) {
            return u16::MAX;
        }
    }

    op
}

// ---- Decompress block ----

fn copy_match(out: &mut [u8], op: &mut u16, hist: &[u8], hist_len: u16, off: u16, mlen: u16) {
    if off <= *op {
        let src = *op - off;
        for j in 0..mlen {
            out[(*op + j) as usize] = out[(src + j) as usize];
        }
    } else {
        let hist_back = off - *op;
        let hist_start = hist_len - hist_back;
        for j in 0..mlen {
            let src = hist_start + j;
            if src < hist_len {
                out[(*op + j) as usize] = hist[src as usize];
            } else {
                out[(*op + j) as usize] = out[(src - hist_len) as usize];
            }
        }
    }
    *op += mlen;
}

fn decompress_block(
    hist: &[u8],
    hist_len: u16,
    input: &[u8],
    in_len: u16,
    out: &mut [u8],
    out_len: u16,
) -> Result<(), Error> {
    let mut ip: u16 = 0;
    let mut op: u16 = 0;
    let mut last_offset: u16 = 0;

    while ip < in_len {
        let token = input[ip as usize];
        ip += 1;

        // 0x00..0x3F: short literal
        if token < 0x40 {
            let lit_len = ((token & 0x3F) + 1) as u16;
            if (ip as u32) + (lit_len as u32) > in_len as u32
                || (op as u32) + (lit_len as u32) > out_len as u32
            {
                return Err(Error::Corrupt);
            }
            out[op as usize..(op + lit_len) as usize]
                .copy_from_slice(&input[ip as usize..(ip + lit_len) as usize]);
            ip += lit_len;
            op += lit_len;
            continue;
        }

        // 0x40..0x7F: dictionary reference (0..63)
        if token < 0x80 {
            let idx = (token & 0x3F) as usize;
            if idx >= DICT_COUNT {
                return Err(Error::Corrupt);
            }
            let entry = &STATIC_DICT[idx];
            let dlen = entry.data.len() as u16;
            if (op as u32) + (dlen as u32) > out_len as u32 {
                return Err(Error::Corrupt);
            }
            out[op as usize..(op + dlen) as usize].copy_from_slice(entry.data);
            op += dlen;
            continue;
        }

        // 0x80..0xBF: LZ match
        if token < 0xC0 {
            if ip >= in_len {
                return Err(Error::Corrupt);
            }
            let mlen = (((token >> 1) & 0x1F) as u16) + MATCH_MIN;
            let off =
                (((token & 0x01) as u16) << 8) | (input[ip as usize] as u16);
            ip += 1;

            if off == 0 {
                return Err(Error::Corrupt);
            }
            if off > op + hist_len {
                return Err(Error::Corrupt);
            }
            if (op as u32) + (mlen as u32) > out_len as u32 {
                return Err(Error::Corrupt);
            }
            copy_match(out, &mut op, hist, hist_len, off, mlen);
            last_offset = off;
            continue;
        }

        // 0xC0..0xCF: repeat-offset match
        if token < 0xD0 {
            let mlen = ((token & 0x0F) as u16) + MATCH_MIN;
            if last_offset == 0 {
                return Err(Error::Corrupt);
            }
            if last_offset > op + hist_len {
                return Err(Error::Corrupt);
            }
            if (op as u32) + (mlen as u32) > out_len as u32 {
                return Err(Error::Corrupt);
            }
            copy_match(out, &mut op, hist, hist_len, last_offset, mlen);
            continue;
        }

        // 0xD0..0xDF: dictionary (80..95)
        if token < 0xE0 {
            let idx = 80 + (token & 0x0F) as usize;
            if idx >= DICT_COUNT {
                return Err(Error::Corrupt);
            }
            let entry = &STATIC_DICT[idx];
            let dlen = entry.data.len() as u16;
            if (op as u32) + (dlen as u32) > out_len as u32 {
                return Err(Error::Corrupt);
            }
            out[op as usize..(op + dlen) as usize].copy_from_slice(entry.data);
            op += dlen;
            continue;
        }

        // 0xE0..0xEF: dictionary (64..79)
        if token < 0xF0 {
            let idx = 64 + (token & 0x0F) as usize;
            if idx >= DICT_COUNT {
                return Err(Error::Corrupt);
            }
            let entry = &STATIC_DICT[idx];
            let dlen = entry.data.len() as u16;
            if (op as u32) + (dlen as u32) > out_len as u32 {
                return Err(Error::Corrupt);
            }
            out[op as usize..(op + dlen) as usize].copy_from_slice(entry.data);
            op += dlen;
            continue;
        }

        // 0xF0..0xFF: long-offset LZ
        {
            let mlen = ((token & 0x0F) as u16) + LONG_MATCH_MIN;
            if (ip as u32) + 2 > in_len as u32 {
                return Err(Error::Corrupt);
            }
            let off = ((input[ip as usize] as u16) << 8)
                | (input[(ip + 1) as usize] as u16);
            ip += 2;

            if off == 0 {
                return Err(Error::Corrupt);
            }
            if off > op + hist_len {
                return Err(Error::Corrupt);
            }
            if (op as u32) + (mlen as u32) > out_len as u32 {
                return Err(Error::Corrupt);
            }
            copy_match(out, &mut op, hist, hist_len, off, mlen);
            last_offset = off;
        }
    }

    if op != out_len {
        return Err(Error::Corrupt);
    }
    Ok(())
}

// ---- History update ----

fn update_history(hist: &mut Vec<u8>, hist_size: u16, data: &[u8]) {
    let len = data.len();
    let max = hist_size as usize;
    if len >= max {
        hist.clear();
        hist.extend_from_slice(&data[len - max..]);
    } else if hist.len() + len <= max {
        hist.extend_from_slice(data);
    } else {
        let keep = max - len;
        let keep = if keep > hist.len() { hist.len() } else { keep };
        let start = hist.len() - keep;
        let kept: Vec<u8> = hist[start..].to_vec();
        hist.clear();
        hist.extend_from_slice(&kept);
        hist.extend_from_slice(data);
    }
}

// ---- Encoder ----

struct Encoder {
    block: Vec<u8>,
    history: Vec<u8>,
    block_size: u16,
    history_size: u16,
    hash_bits: u32,
    hash_chain_depth: usize,
    lazy_steps: u16,
}

impl Encoder {
    fn new(opts: &Options) -> Self {
        Self {
            block: Vec::with_capacity(opts.block_size as usize),
            history: Vec::new(),
            block_size: opts.block_size,
            history_size: opts.history_size,
            hash_bits: opts.hash_bits,
            hash_chain_depth: opts.hash_chain_depth,
            lazy_steps: opts.lazy_steps,
        }
    }

    fn sink(&mut self, data: &[u8], output: &mut Vec<u8>) {
        let mut pos = 0usize;
        while pos < data.len() {
            let room = self.block_size as usize - self.block.len();
            let take = core::cmp::min(data.len() - pos, room);
            self.block.extend_from_slice(&data[pos..pos + take]);
            pos += take;

            if self.block.len() == self.block_size as usize {
                self.flush(output);
            }
        }
    }

    fn finish(&mut self, output: &mut Vec<u8>) {
        if !self.block.is_empty() {
            self.flush(output);
        }
    }

    fn flush(&mut self, output: &mut Vec<u8>) {
        if self.block.is_empty() {
            return;
        }

        let raw_len = self.block.len() as u16;
        let hist_len = self.history.len() as u16;
        let hash_mask = ((1u32 << self.hash_bits) - 1) as u16;

        // Build combined buffer [history | block]
        let mut combined = Vec::with_capacity((hist_len + raw_len) as usize);
        combined.extend_from_slice(&self.history);
        combined.extend_from_slice(&self.block);

        let mut tmp = vec![0u8; BLOCK_MAX_COMPRESSED];
        let comp_len = compress_block(
            &combined,
            hist_len,
            raw_len,
            &mut tmp,
            self.hash_chain_depth,
            hash_mask,
            self.lazy_steps,
        );

        // Update history for next block
        update_history(&mut self.history, self.history_size, &self.block);

        // Write header
        if comp_len == u16::MAX || comp_len >= raw_len {
            // Raw fallback
            output.push((raw_len & 0xFF) as u8);
            output.push((raw_len >> 8) as u8);
            output.push(0);
            output.push(0);
            output.extend_from_slice(&self.block);
        } else {
            output.push((raw_len & 0xFF) as u8);
            output.push((raw_len >> 8) as u8);
            output.push((comp_len & 0xFF) as u8);
            output.push((comp_len >> 8) as u8);
            output.extend_from_slice(&tmp[..comp_len as usize]);
        }

        self.block.clear();
    }
}

// ---- Decoder ----

struct Decoder {
    header: [u8; 4],
    header_len: u8,
    raw_len: u16,
    comp_len: u16,
    payload: Vec<u8>,
    history: Vec<u8>,
}

impl Decoder {
    fn new() -> Self {
        Self {
            header: [0u8; 4],
            header_len: 0,
            raw_len: 0,
            comp_len: 0,
            payload: Vec::new(),
            history: Vec::new(),
        }
    }

    fn sink(&mut self, data: &[u8], output: &mut Vec<u8>) -> Result<(), Error> {
        let mut pos = 0usize;

        while pos < data.len() {
            // Read header
            if self.header_len < 4 {
                let need = 4 - self.header_len as usize;
                let take = core::cmp::min(data.len() - pos, need);
                self.header[self.header_len as usize..self.header_len as usize + take]
                    .copy_from_slice(&data[pos..pos + take]);
                self.header_len += take as u8;
                pos += take;

                if self.header_len < 4 {
                    continue;
                }

                self.raw_len =
                    (self.header[0] as u16) | ((self.header[1] as u16) << 8);
                self.comp_len =
                    (self.header[2] as u16) | ((self.header[3] as u16) << 8);
                self.payload.clear();

                if self.raw_len == 0 && self.comp_len == 0 {
                    self.header_len = 0;
                    continue;
                }
                if self.raw_len == 0 || self.raw_len > BLOCK_SIZE {
                    return Err(Error::Corrupt);
                }
                if self.comp_len > 0 && self.comp_len > BLOCK_MAX_COMPRESSED as u16 {
                    return Err(Error::Corrupt);
                }
            }

            // Read payload
            let target = if self.comp_len == 0 {
                self.raw_len
            } else {
                self.comp_len
            };
            let need = target as usize - self.payload.len();
            let take = core::cmp::min(data.len() - pos, need);
            self.payload.extend_from_slice(&data[pos..pos + take]);
            pos += take;

            if self.payload.len() == target as usize {
                self.emit_block(output)?;
                self.header_len = 0;
                self.raw_len = 0;
                self.comp_len = 0;
                self.payload.clear();
            }
        }

        Ok(())
    }

    fn emit_block(&mut self, output: &mut Vec<u8>) -> Result<(), Error> {
        if self.comp_len == 0 {
            // Stored raw
            output.extend_from_slice(&self.payload);
            update_history(&mut self.history, HISTORY_SIZE, &self.payload);
            Ok(())
        } else {
            let hist_len = self.history.len() as u16;
            let mut raw = vec![0u8; self.raw_len as usize];
            decompress_block(
                &self.history,
                hist_len,
                &self.payload,
                self.comp_len,
                &mut raw,
                self.raw_len,
            )?;
            output.extend_from_slice(&raw);
            update_history(&mut self.history, HISTORY_SIZE, &raw);
            Ok(())
        }
    }

    fn finish(&self) -> Result<(), Error> {
        if self.header_len != 0 || self.raw_len != 0 || self.comp_len != 0 || !self.payload.is_empty()
        {
            return Err(Error::Corrupt);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::format;

    #[test]
    fn roundtrip_empty() {
        let data = b"";
        let compressed = compress(data);
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed, data);
    }

    #[test]
    fn roundtrip_hello() {
        let data = b"hello world";
        let compressed = compress(data);
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed, data);
    }

    #[test]
    fn roundtrip_repetitive() {
        let data = b"abcabcabcabcabcabcabcabcabcabc";
        let compressed = compress(data);
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed, data);
    }

    #[test]
    fn roundtrip_json() {
        let data = br#"{"name":"value","type":"string","data":"content","status":"active"}"#;
        let compressed = compress(data);
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn roundtrip_large() {
        let mut data = Vec::new();
        for i in 0..2000u16 {
            data.extend_from_slice(format!("item_{:04} ", i).as_bytes());
        }
        let compressed = compress(&data);
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(decompressed, data);
    }

    #[test]
    fn decompress_corrupt() {
        assert!(decompress(&[0xFF, 0xFF, 0x01, 0x00, 0x80]).is_err());
    }

    #[test]
    fn options_builder() {
        let opts = Options::new().block_size(256).history_size(128).lazy_steps(2);
        let data = b"test data for compression with options builder pattern";
        let compressed = compress_with(data, &opts);
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn dict_entries_count() {
        assert_eq!(STATIC_DICT.len(), 96);
    }
}
