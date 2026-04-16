#include "picocompress.h"

#include <string.h>

#define PC_HASH_SIZE (1u << PC_HASH_BITS)
#define PC_INVALID_POS (-1)
#define PC_GOOD_MATCH 8u
#define PC_REPEAT_CACHE_SIZE 3u

static uint16_t pc_hash3(const uint8_t *p) {
    uint32_t v = ((uint32_t)p[0] * 251u) + ((uint32_t)p[1] * 11u) + ((uint32_t)p[2] * 3u);
    return (uint16_t)(v & (PC_HASH_SIZE - 1u));
}

static uint16_t pc_match_len(const uint8_t *a, const uint8_t *b, uint16_t limit) {
    uint16_t m = 0;
    while (m < limit && a[m] == b[m]) {
        ++m;
    }
    return m;
}

static int pc_emit_literals(
    const uint8_t *src,
    uint16_t src_len,
    uint8_t *dst,
    uint16_t dst_cap,
    uint16_t *op
) {
    uint16_t pos = 0;
    while (pos < src_len) {
        uint16_t chunk = (uint16_t)(src_len - pos);
        if (chunk > PC_LITERAL_EXT_MAX) {
            chunk = PC_LITERAL_EXT_MAX;
        }
        if ((uint32_t)(*op) + 1u + chunk > dst_cap) {
            return 0;
        }
        if (chunk <= PC_LITERAL_MAX) {
            dst[(*op)++] = (uint8_t)(chunk - 1u);           /* 0x00..0x3F */
        } else {
            dst[(*op)++] = (uint8_t)(0xE0u | (chunk - 65u)); /* 0xE0..0xFF */
        }
        memcpy(dst + *op, src + pos, chunk);
        *op = (uint16_t)(*op + chunk);
        pos = (uint16_t)(pos + chunk);
    }
    return 1;
}

static void pc_head_insert(
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE],
    uint16_t hash,
    int16_t pos
) {
    int d;
    for (d = (int)PC_HASH_CHAIN_DEPTH - 1; d > 0; --d) {
        head[d][hash] = head[d - 1][hash];
    }
    head[0][hash] = pos;
}

typedef struct {
    const uint8_t *data;
    uint8_t len;
} pc_dict_entry_t;

/* ---- general-purpose static dictionary (64 entries, ROM/flash) --------
 * Token format v3 — clean 2-bit type prefix:
 *   0x00..0x3F  short literal   (len 1..64)
 *   0x40..0x7F  dictionary ref  (index 0..63)
 *   0x80..0xBF  LZ match        (5-bit len + 1-bit offset_hi)
 *   0xC0..0xDF  repeat-offset   (5-bit len)
 *   0xE0..0xFF  extended literal (len 65..96)
 * -------------------------------------------------------------------- */

/* 0-3: single-byte ultra-common symbols */
static const uint8_t pc_d00[] = "{";
static const uint8_t pc_d01[] = "}";
static const uint8_t pc_d02[] = ":";
static const uint8_t pc_d03[] = ",";
/* 4-7: two-byte common pairs */
static const uint8_t pc_d04[] = { '\r', '\n' };
static const uint8_t pc_d05[] = "id";
static const uint8_t pc_d06[] = { '"', ':' };              /* ":   */
static const uint8_t pc_d07[] = { ',', '"' };              /* ,"   */
/* 8-15: three-byte patterns */
static const uint8_t pc_d08[] = { '"', ':', '"' };         /* ":"  ← JSON money pattern */
static const uint8_t pc_d09[] = "000";
static const uint8_t pc_d10[] = "ORD";
static const uint8_t pc_d11[] = "the";
static const uint8_t pc_d12[] = "ing";
static const uint8_t pc_d13[] = { ',', '"', ',' };         /* ","  JSON string separator */
static const uint8_t pc_d14[] = { '"', ':', '{' };         /* ":{  nested obj  */
static const uint8_t pc_d15[] = { '"', ':', '[' };         /* ":[  nested arr  */
/* 16-23: more three-byte */
static const uint8_t pc_d16[] = "ion";
static const uint8_t pc_d17[] = "ent";
static const uint8_t pc_d18[] = "ter";
static const uint8_t pc_d19[] = "and";
static const uint8_t pc_d20[] = "00";                      /* ASCII "00" */
static const uint8_t pc_d21[] = { '"', '}', ',' };         /* "},  */
static const uint8_t pc_d22[] = { '"', ']', ',' };         /* "],  */
static const uint8_t pc_d23[] = "  ";                      /* double space */
/* 24-39: four-byte */
static const uint8_t pc_d24[] = { 'n','o','"',':' };       /* no": */
static const uint8_t pc_d25[] = "true";
static const uint8_t pc_d26[] = "null";
static const uint8_t pc_d27[] = "name";
static const uint8_t pc_d28[] = "data";
static const uint8_t pc_d29[] = "time";
static const uint8_t pc_d30[] = "type";
static const uint8_t pc_d31[] = "mode";
static const uint8_t pc_d32[] = "http";
static const uint8_t pc_d33[] = "tion";
static const uint8_t pc_d34[] = "code";
static const uint8_t pc_d35[] = "size";
static const uint8_t pc_d36[] = "ment";
static const uint8_t pc_d37[] = "list";
static const uint8_t pc_d38[] = "item";
static const uint8_t pc_d39[] = "text";
/* 40-47: five-byte */
static const uint8_t pc_d40[] = "false";
static const uint8_t pc_d41[] = "error";
static const uint8_t pc_d42[] = "value";
static const uint8_t pc_d43[] = "state";
static const uint8_t pc_d44[] = "alert";
static const uint8_t pc_d45[] = "input";
static const uint8_t pc_d46[] = "ation";
static const uint8_t pc_d47[] = "order";
/* 48-55: six-byte */
static const uint8_t pc_d48[] = "status";
static const uint8_t pc_d49[] = "number";
static const uint8_t pc_d50[] = "active";
static const uint8_t pc_d51[] = "device";
static const uint8_t pc_d52[] = "region";
static const uint8_t pc_d53[] = "string";
static const uint8_t pc_d54[] = "result";
static const uint8_t pc_d55[] = "length";
/* 56-59: seven-byte */
static const uint8_t pc_d56[] = "message";
static const uint8_t pc_d57[] = "content";
static const uint8_t pc_d58[] = "request";
static const uint8_t pc_d59[] = "default";
/* 60-63: eight-byte */
static const uint8_t pc_d60[] = { 'n','u','m','b','e','r','"',':' }; /* number": */
static const uint8_t pc_d61[] = "operator";
static const uint8_t pc_d62[] = { 'h','t','t','p','s',':','/','/'}; /* https:// */
static const uint8_t pc_d63[] = "response";

static const pc_dict_entry_t pc_static_dict[PC_DICT_COUNT] = {
    /* 0-3:  1B */  { pc_d00,1 }, { pc_d01,1 }, { pc_d02,1 }, { pc_d03,1 },
    /* 4-7:  2B */  { pc_d04,2 }, { pc_d05,2 }, { pc_d06,2 }, { pc_d07,2 },
    /* 8-15: 3B */  { pc_d08,3 }, { pc_d09,3 }, { pc_d10,3 }, { pc_d11,3 },
                    { pc_d12,3 }, { pc_d13,3 }, { pc_d14,3 }, { pc_d15,3 },
    /* 16-23:3B */  { pc_d16,3 }, { pc_d17,3 }, { pc_d18,3 }, { pc_d19,3 },
                    { pc_d20,2 }, { pc_d21,3 }, { pc_d22,3 }, { pc_d23,2 },
    /* 24-39:4B */  { pc_d24,4 }, { pc_d25,4 }, { pc_d26,4 }, { pc_d27,4 },
                    { pc_d28,4 }, { pc_d29,4 }, { pc_d30,4 }, { pc_d31,4 },
                    { pc_d32,4 }, { pc_d33,4 }, { pc_d34,4 }, { pc_d35,4 },
                    { pc_d36,4 }, { pc_d37,4 }, { pc_d38,4 }, { pc_d39,4 },
    /* 40-47:5B */  { pc_d40,5 }, { pc_d41,5 }, { pc_d42,5 }, { pc_d43,5 },
                    { pc_d44,5 }, { pc_d45,5 }, { pc_d46,5 }, { pc_d47,5 },
    /* 48-55:6B */  { pc_d48,6 }, { pc_d49,6 }, { pc_d50,6 }, { pc_d51,6 },
                    { pc_d52,6 }, { pc_d53,6 }, { pc_d54,6 }, { pc_d55,6 },
    /* 56-59:7B */  { pc_d56,7 }, { pc_d57,7 }, { pc_d58,7 }, { pc_d59,7 },
    /* 60-63:8B */  { pc_d60,8 }, { pc_d61,8 }, { pc_d62,8 }, { pc_d63,8 },
};

/* Find best savings among repeat-cache, dict, and LZ at a virtual position.
 * Order: repeat-cache → dictionary → hash-chain LZ.
 * Returns net savings (bytes saved vs literal). Fills out_* params. */
static int pc_find_best(
    const uint8_t *vbuf, uint16_t vbuf_len, uint16_t vpos,
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE],
    const uint16_t rep_offsets[PC_REPEAT_CACHE_SIZE],
    uint16_t *out_len, uint16_t *out_off, uint16_t *out_dict,
    int *out_is_repeat
) {
    int best_savings = 0;
    uint16_t remaining = (uint16_t)(vbuf_len - vpos);
    int d;

    *out_len = 0;
    *out_off = 0;
    *out_dict = UINT16_MAX;
    *out_is_repeat = 0;

    /* 1. Repeat-offset cache — try recent offsets first (1-byte token each).
     * These fire constantly on structured data. Check first byte before
     * full compare (idea #9: early reject).
     * NOTE: only rep_offsets[0] can emit as a repeat token (decoder tracks
     * only last_offset). Matches on [1]/[2] are scored as normal LZ. */
    if (remaining >= PC_MATCH_MIN) {
        uint16_t max_rep = remaining > PC_MATCH_MAX ? PC_MATCH_MAX : remaining;
        for (d = 0; d < (int)PC_REPEAT_CACHE_SIZE; ++d) {
            uint16_t off = rep_offsets[d];
            uint16_t len;
            int is_rep, token_cost, s;
            if (off == 0u || off > vpos) continue;
            /* early reject: check first byte */
            if (vbuf[vpos] != vbuf[vpos - off]) continue;
            /* fast path for len 2-3 (idea #2) */
            if (remaining >= 2u && vbuf[vpos + 1u] != vbuf[vpos - off + 1u]) continue;
            len = pc_match_len(vbuf + vpos - off, vbuf + vpos, max_rep);
            if (len < PC_MATCH_MIN) continue;

            /* only slot 0 can use the cheap repeat token */
            is_rep = (d == 0) ? 1 : 0;
            token_cost = is_rep ? 1 : (off <= PC_OFFSET_SHORT_MAX ? 2 : 3);
            s = (int)len - token_cost;

            if (s > best_savings) {
                best_savings = s;
                *out_len = len;
                *out_off = off;
                *out_dict = UINT16_MAX;
                *out_is_repeat = is_rep;
                if (len >= PC_GOOD_MATCH) return best_savings; /* #10: good enough */
            }
        }
    }

    /* 2. Dictionary match (1-byte token → savings = len - 1).
     * First-byte filter + early bail on good-enough (idea #3, #10). */
    {
        uint8_t first_byte = vbuf[vpos];
        for (d = 0; d < (int)PC_DICT_COUNT; ++d) {
            uint8_t dlen = pc_static_dict[d].len;
            int s;
            if (dlen > remaining) continue;
            if ((int)dlen - 1 <= best_savings) continue;
            if (pc_static_dict[d].data[0] != first_byte) continue;
            if (memcmp(vbuf + vpos, pc_static_dict[d].data, dlen) != 0) continue;
            s = (int)dlen - 1;
            best_savings = s;
            *out_dict = (uint16_t)d;
            *out_len = dlen;
            *out_off = 0;
            *out_is_repeat = 0;
            if (dlen >= PC_GOOD_MATCH) return best_savings; /* #10 */
        }
    }

    /* 3. LZ hash-chain match — with early reject (#9), offset scoring (#5),
     * good-enough bail (#10). */
    if (remaining >= 3u) {
        uint16_t hash = pc_hash3(vbuf + vpos);
        uint16_t max_len_short = remaining > PC_MATCH_MAX ? PC_MATCH_MAX : remaining;
        uint16_t max_len_long = remaining > PC_LONG_MATCH_MAX ? PC_LONG_MATCH_MAX : remaining;
        uint8_t first_byte = vbuf[vpos];

        for (d = 0; d < (int)PC_HASH_CHAIN_DEPTH; ++d) {
            int16_t prev = head[d][hash];
            uint16_t prev_pos, off, len, max_len;
            int s, token_cost;

            if (prev < 0) continue;
            prev_pos = (uint16_t)prev;
            if (prev_pos >= vpos) continue;
            off = (uint16_t)(vpos - prev_pos);
            if (off == 0u || off > PC_OFFSET_LONG_MAX) continue;

            /* #9: early reject — check first byte before full compare */
            if (vbuf[prev_pos] != first_byte) continue;

            max_len = (off <= PC_OFFSET_SHORT_MAX) ? max_len_short : max_len_long;
            len = pc_match_len(vbuf + prev_pos, vbuf + vpos, max_len);
            if (len < PC_MATCH_MIN) continue;

            token_cost = (off <= PC_OFFSET_SHORT_MAX) ? 2 : 3;
            s = (int)len - token_cost;

            /* #5: offset scoring — prefer nearer matches at equal savings */
            if (s > best_savings
                || (s == best_savings && len > *out_len)
                || (s == best_savings && len == *out_len && off < *out_off)) {
                best_savings = s;
                *out_len = len;
                *out_off = off;
                *out_dict = UINT16_MAX;
                *out_is_repeat = 0;
                if (len >= PC_GOOD_MATCH) return best_savings; /* #10 */
            }
        }
    }

    return best_savings;
}

/* Compress block_len bytes from vbuf starting at offset hist_len.
 * vbuf = [history(hist_len) | block(block_len)].
 * Returns compressed size, or UINT16_MAX on overflow. */
static uint16_t pc_compress_block(
    const uint8_t *vbuf,
    uint16_t hist_len,
    uint16_t block_len,
    uint8_t *out,
    uint16_t out_cap
) {
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE];
    uint16_t rep_offsets[PC_REPEAT_CACHE_SIZE] = {0, 0, 0};
    uint16_t vbuf_len = (uint16_t)(hist_len + block_len);
    uint16_t vpos;
    uint16_t anchor;
    uint16_t op = 0;
    memset(head, 0xFF, sizeof(head));

    /* seed hash table from history — use normal insert so chain works */
    if (hist_len >= 3u) {
        uint16_t p;
        for (p = 0; (uint16_t)(p + 2u) < hist_len; ++p) {
            pc_head_insert(head, pc_hash3(vbuf + p), (int16_t)p);
        }
        /* Re-inject positions near the block boundary into slot 0 */
        {
            uint16_t tail_start = hist_len > 64u ? (uint16_t)(hist_len - 64u) : 0u;
            for (p = tail_start; (uint16_t)(p + 2u) < hist_len; ++p) {
                uint16_t h = pc_hash3(vbuf + p);
                if (head[0][h] != (int16_t)p) {
                    int16_t save = head[PC_HASH_CHAIN_DEPTH - 1u][h];
                    pc_head_insert(head, h, (int16_t)p);
                    head[PC_HASH_CHAIN_DEPTH - 1u][h] = save;
                }
            }
        }
    }

    anchor = hist_len;
    vpos = hist_len;
    while (vpos < vbuf_len) {
        uint16_t best_len, best_off, best_dict;
        int best_is_repeat, best_savings;
retry_pos:
        best_len = 0; best_off = 0; best_dict = UINT16_MAX;
        best_is_repeat = 0;

        if ((uint16_t)(vbuf_len - vpos) < PC_MATCH_MIN) {
            break;
        }

        best_savings = pc_find_best(
            vbuf, vbuf_len, vpos, head, rep_offsets,
            &best_len, &best_off, &best_dict, &best_is_repeat);

        /* insert current position into hash table (needs 3 bytes) */
        if ((uint16_t)(vbuf_len - vpos) >= 3u) {
            pc_head_insert(head, pc_hash3(vbuf + vpos), (int16_t)vpos);
        }

        /* #3: short dict entries save a literal-header byte when standalone */
        if (best_dict != UINT16_MAX && best_savings == 0 && anchor == vpos) {
            best_savings = 1;
        }

        /* #7: literal run extension — skip weak matches (savings <= 1) when
         * we're mid-literal-run, because the match token overhead isn't
         * worth breaking the run for a tiny gain. */
        if (best_savings <= 1 && best_dict == UINT16_MAX && anchor < vpos) {
            best_savings = 0;
        }

        /* #4: lazy matching — only if current match is short.
         * Long matches (>= GOOD_MATCH) are rarely beaten; accept immediately. */
        if (best_savings > 0 && best_len < PC_GOOD_MATCH) {
            uint16_t step;
            for (step = 1; step <= (uint16_t)PC_LAZY_STEPS; ++step) {
                uint16_t npos = (uint16_t)(vpos + step);
                if (npos >= vbuf_len || (uint16_t)(vbuf_len - npos) < PC_MATCH_MIN)
                    break;
                {
                    uint16_t n_len, n_off, n_dict;
                    int n_rep;
                    int n_sav = pc_find_best(
                        vbuf, vbuf_len, npos, head, rep_offsets,
                        &n_len, &n_off, &n_dict, &n_rep);
                    if (n_sav > best_savings) {
                        uint16_t s;
                        for (s = 0; s < step; ++s) {
                            uint16_t sp = (uint16_t)(vpos + s);
                            if ((uint16_t)(vbuf_len - sp) >= 3u)
                                pc_head_insert(head, pc_hash3(vbuf + sp), (int16_t)sp);
                        }
                        vpos = npos;
                        goto retry_pos;
                    }
                }
            }
        }

        /* emit */
        if (best_savings > 0) {
            uint16_t lit_len = (uint16_t)(vpos - anchor);
            uint16_t k;

            if (!pc_emit_literals(vbuf + anchor, lit_len, out, out_cap, &op)) {
                return UINT16_MAX;
            }

            if (best_dict != UINT16_MAX) {
                if ((uint32_t)op + 1u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(0x40u | (best_dict & 0x3Fu));
            } else if (best_is_repeat) {
                if ((uint32_t)op + 1u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(0xC0u | ((best_len - PC_MATCH_MIN) & 0x1Fu));
            } else if (best_off <= PC_OFFSET_SHORT_MAX && best_len <= PC_MATCH_MAX) {
                /* short-offset LZ: 2-byte token */
                if ((uint32_t)op + 2u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(
                    0x80u
                    | (((best_len - PC_MATCH_MIN) & 0x1Fu) << 1u)
                    | ((best_off >> 8u) & 0x01u));
                out[op++] = (uint8_t)(best_off & 0xFFu);
            } else {
                /* long-offset LZ: 3-byte token (0xF0..0xFF) */
                uint16_t elen = best_len > PC_LONG_MATCH_MAX ? PC_LONG_MATCH_MAX : best_len;
                if ((uint32_t)op + 3u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(0xF0u | ((elen - PC_LONG_MATCH_MIN) & 0x0Fu));
                out[op++] = (uint8_t)((best_off >> 8u) & 0xFFu);
                out[op++] = (uint8_t)(best_off & 0xFFu);
                best_len = elen;
            }

            /* update repeat-offset cache (#1) */
            if (!best_is_repeat && best_off != 0u && best_dict == UINT16_MAX) {
                rep_offsets[2] = rep_offsets[1];
                rep_offsets[1] = rep_offsets[0];
                rep_offsets[0] = best_off;
            }

            for (k = 1; k < best_len && (uint16_t)(vpos + k + 2u) < vbuf_len; ++k) {
                pc_head_insert(head, pc_hash3(vbuf + vpos + k), (int16_t)(vpos + k));
            }

            vpos = (uint16_t)(vpos + best_len);
            anchor = vpos;
        } else {
            ++vpos;
        }
    }

    if (anchor < vbuf_len) {
        if (!pc_emit_literals(vbuf + anchor, (uint16_t)(vbuf_len - anchor), out, out_cap, &op)) {
            return UINT16_MAX;
        }
    }

    return op;
}

/* Copy match bytes, resolving cross-block references into history. */
static void pc_copy_match(
    uint8_t *out, uint16_t *op_p,
    const uint8_t *hist, uint16_t hist_len,
    uint16_t off, uint16_t match_len
) {
    uint16_t op = *op_p;
    uint16_t j;
    if (off <= op) {
        /* entirely within current block output */
        uint16_t src = (uint16_t)(op - off);
        for (j = 0; j < match_len; ++j) {
            out[op++] = out[src + j];
        }
    } else {
        /* starts in history, may cross into current output */
        uint16_t hist_back = (uint16_t)(off - op);
        uint16_t hist_start = (uint16_t)(hist_len - hist_back);
        for (j = 0; j < match_len; ++j) {
            uint16_t src = (uint16_t)(hist_start + j);
            if (src < hist_len) {
                out[op++] = hist[src];
            } else {
                out[op++] = out[src - hist_len];
            }
        }
    }
    *op_p = op;
}

static pc_result pc_decompress_block(
    const uint8_t *hist, uint16_t hist_len,
    const uint8_t *in, uint16_t in_len,
    uint8_t *out, uint16_t out_len
) {
    uint16_t ip = 0;
    uint16_t op = 0;
    uint16_t last_offset = 0;

    while (ip < in_len) {
        uint8_t token = in[ip++];

        /* 0x00..0x3F: short literal run (1..64) */
        if (token < 0x40u) {
            uint16_t lit_len = (uint16_t)((token & 0x3Fu) + 1u);
            if ((uint32_t)ip + lit_len > in_len || (uint32_t)op + lit_len > out_len) {
                return PC_ERR_CORRUPT;
            }
            memcpy(out + op, in + ip, lit_len);
            ip = (uint16_t)(ip + lit_len);
            op = (uint16_t)(op + lit_len);
            continue;
        }

        /* 0x40..0x7F: dictionary reference (0..63) */
        if (token < 0x80u) {
            uint16_t idx = (uint16_t)(token & 0x3Fu);
            uint8_t dlen;
            if (idx >= PC_DICT_COUNT) return PC_ERR_CORRUPT;
            dlen = pc_static_dict[idx].len;
            if ((uint32_t)op + dlen > out_len) return PC_ERR_CORRUPT;
            memcpy(out + op, pc_static_dict[idx].data, dlen);
            op = (uint16_t)(op + dlen);
            continue;
        }

        /* 0x80..0xBF: LZ match with explicit offset */
        if (token < 0xC0u) {
            uint16_t match_len, off;
            if (ip >= in_len) return PC_ERR_CORRUPT;
            match_len = (uint16_t)(((token >> 1u) & 0x1Fu) + PC_MATCH_MIN);
            off = (uint16_t)(((uint16_t)(token & 0x01u) << 8u) | (uint16_t)in[ip++]);

            if (off == 0u) return PC_ERR_CORRUPT;
            if (off > (uint16_t)(op + hist_len)) return PC_ERR_CORRUPT;
            if ((uint32_t)op + match_len > out_len) return PC_ERR_CORRUPT;

            pc_copy_match(out, &op, hist, hist_len, off, match_len);
            last_offset = off;
            continue;
        }

        /* 0xC0..0xDF: repeat-offset match */
        if (token < 0xE0u) {
            uint16_t match_len = (uint16_t)((token & 0x1Fu) + PC_MATCH_MIN);
            if (last_offset == 0u) return PC_ERR_CORRUPT;
            if (last_offset > (uint16_t)(op + hist_len)) return PC_ERR_CORRUPT;
            if ((uint32_t)op + match_len > out_len) return PC_ERR_CORRUPT;
            pc_copy_match(out, &op, hist, hist_len, last_offset, match_len);
            continue;
        }

        /* 0xE0..0xEF: extended literal run (65..80) */
        if (token < 0xF0u) {
            uint16_t lit_len = (uint16_t)((token & 0x0Fu) + 65u);
            if ((uint32_t)ip + lit_len > in_len || (uint32_t)op + lit_len > out_len) {
                return PC_ERR_CORRUPT;
            }
            memcpy(out + op, in + ip, lit_len);
            ip = (uint16_t)(ip + lit_len);
            op = (uint16_t)(op + lit_len);
            continue;
        }

        /* 0xF0..0xFF: long-offset LZ match (3-byte token) */
        {
            uint16_t match_len = (uint16_t)((token & 0x0Fu) + PC_LONG_MATCH_MIN);
            uint16_t off;
            if ((uint32_t)ip + 2u > in_len) return PC_ERR_CORRUPT;
            off = (uint16_t)(((uint16_t)in[ip] << 8u) | (uint16_t)in[ip + 1u]);
            ip = (uint16_t)(ip + 2u);

            if (off == 0u) return PC_ERR_CORRUPT;
            if (off > (uint16_t)(op + hist_len)) return PC_ERR_CORRUPT;
            if ((uint32_t)op + match_len > out_len) return PC_ERR_CORRUPT;

            pc_copy_match(out, &op, hist, hist_len, off, match_len);
            last_offset = off;
        }
    }

    if (op != out_len) {
        return PC_ERR_CORRUPT;
    }
    return PC_OK;
}

static pc_result pc_write_all(pc_write_fn write_fn, void *user, const uint8_t *data, size_t len) {
    if (len == 0) {
        return PC_OK;
    }
    if (write_fn == NULL) {
        return PC_ERR_INPUT;
    }
    return write_fn(user, data, len) == 0 ? PC_OK : PC_ERR_WRITE;
}

static void pc_update_history(uint8_t *hist, uint16_t *hist_len, const uint8_t *data, uint16_t len) {
    if (len >= (uint16_t)PC_HISTORY_SIZE) {
        memcpy(hist, data + len - (uint16_t)PC_HISTORY_SIZE, PC_HISTORY_SIZE);
        *hist_len = (uint16_t)PC_HISTORY_SIZE;
    } else if ((uint16_t)(*hist_len + len) <= (uint16_t)PC_HISTORY_SIZE) {
        memcpy(hist + *hist_len, data, len);
        *hist_len = (uint16_t)(*hist_len + len);
    } else {
        uint16_t keep = (uint16_t)(PC_HISTORY_SIZE - len);
        if (keep > *hist_len) keep = *hist_len;
        memmove(hist, hist + *hist_len - keep, keep);
        memcpy(hist + keep, data, len);
        *hist_len = (uint16_t)(keep + len);
    }
}

static pc_result pc_encoder_flush(pc_encoder *enc, pc_write_fn write_fn, void *user) {
    uint8_t combined[PC_HISTORY_SIZE + PC_BLOCK_SIZE];
    uint8_t tmp[PC_BLOCK_MAX_COMPRESSED];
    uint8_t header[4];
    uint16_t raw_len;
    uint16_t hist_len;
    uint16_t comp_len;
    pc_result rc;

    if (enc->block_len == 0u) {
        return PC_OK;
    }

    raw_len = enc->block_len;
    hist_len = enc->history_len;

    memcpy(combined, enc->history, hist_len);
    memcpy(combined + hist_len, enc->block, raw_len);

    comp_len = pc_compress_block(combined, hist_len, raw_len, tmp, (uint16_t)sizeof(tmp));

    /* update history for next block */
    pc_update_history(enc->history, &enc->history_len, enc->block, raw_len);

    header[0] = (uint8_t)(raw_len & 0xFFu);
    header[1] = (uint8_t)(raw_len >> 8u);

    if (comp_len == UINT16_MAX || comp_len >= raw_len) {
        header[2] = 0u;
        header[3] = 0u;
        rc = pc_write_all(write_fn, user, header, sizeof(header));
        if (rc != PC_OK) {
            return rc;
        }
        rc = pc_write_all(write_fn, user, enc->block, raw_len);
        if (rc != PC_OK) {
            return rc;
        }
    } else {
        header[2] = (uint8_t)(comp_len & 0xFFu);
        header[3] = (uint8_t)(comp_len >> 8u);
        rc = pc_write_all(write_fn, user, header, sizeof(header));
        if (rc != PC_OK) {
            return rc;
        }
        rc = pc_write_all(write_fn, user, tmp, comp_len);
        if (rc != PC_OK) {
            return rc;
        }
    }

    enc->block_len = 0u;
    return PC_OK;
}

void pc_encoder_init(pc_encoder *enc) {
    if (enc != NULL) {
        enc->block_len = 0u;
        enc->history_len = 0u;
    }
}

pc_result pc_encoder_sink(
    pc_encoder *enc,
    const uint8_t *data,
    size_t len,
    pc_write_fn write_fn,
    void *user
) {
    size_t pos = 0;

    if (enc == NULL || (len > 0u && data == NULL) || write_fn == NULL) {
        return PC_ERR_INPUT;
    }

    while (pos < len) {
        size_t room = (size_t)PC_BLOCK_SIZE - (size_t)enc->block_len;
        size_t take = len - pos;
        if (take > room) {
            take = room;
        }
        memcpy(enc->block + enc->block_len, data + pos, take);
        enc->block_len = (uint16_t)(enc->block_len + (uint16_t)take);
        pos += take;

        if (enc->block_len == (uint16_t)PC_BLOCK_SIZE) {
            pc_result rc = pc_encoder_flush(enc, write_fn, user);
            if (rc != PC_OK) {
                return rc;
            }
        }
    }

    return PC_OK;
}

pc_result pc_encoder_finish(pc_encoder *enc, pc_write_fn write_fn, void *user) {
    if (enc == NULL || write_fn == NULL) {
        return PC_ERR_INPUT;
    }
    return pc_encoder_flush(enc, write_fn, user);
}

void pc_decoder_init(pc_decoder *dec) {
    if (dec != NULL) {
        memset(dec, 0, sizeof(*dec));
    }
}

static pc_result pc_decoder_emit_block(pc_decoder *dec, pc_write_fn write_fn, void *user) {
    if (dec->comp_len == 0u) {
        pc_result rc = pc_write_all(write_fn, user, dec->payload, dec->raw_len);
        if (rc == PC_OK) {
            pc_update_history(dec->history, &dec->history_len, dec->payload, dec->raw_len);
        }
        return rc;
    }
    {
        pc_result rc = pc_decompress_block(
            dec->history, dec->history_len,
            dec->payload, dec->comp_len,
            dec->raw, dec->raw_len);
        if (rc != PC_OK) {
            return rc;
        }
        rc = pc_write_all(write_fn, user, dec->raw, dec->raw_len);
        if (rc == PC_OK) {
            pc_update_history(dec->history, &dec->history_len, dec->raw, dec->raw_len);
        }
        return rc;
    }
}

pc_result pc_decoder_sink(
    pc_decoder *dec,
    const uint8_t *data,
    size_t len,
    pc_write_fn write_fn,
    void *user
) {
    size_t pos = 0;

    if (dec == NULL || (len > 0u && data == NULL) || write_fn == NULL) {
        return PC_ERR_INPUT;
    }

    while (pos < len) {
        if (dec->header_len < 4u) {
            size_t need = 4u - (size_t)dec->header_len;
            size_t take = len - pos;
            if (take > need) {
                take = need;
            }
            memcpy(dec->header + dec->header_len, data + pos, take);
            dec->header_len = (uint8_t)(dec->header_len + (uint8_t)take);
            pos += take;

            if (dec->header_len < 4u) {
                continue;
            }

            dec->raw_len = (uint16_t)(dec->header[0] | ((uint16_t)dec->header[1] << 8u));
            dec->comp_len = (uint16_t)(dec->header[2] | ((uint16_t)dec->header[3] << 8u));
            dec->payload_len = 0u;

            if (dec->raw_len == 0u && dec->comp_len == 0u) {
                dec->header_len = 0u;
                continue;
            }
            if (dec->raw_len == 0u || dec->raw_len > (uint16_t)PC_BLOCK_SIZE) {
                return PC_ERR_CORRUPT;
            }
            if (dec->comp_len > 0u && dec->comp_len > (uint16_t)PC_BLOCK_MAX_COMPRESSED) {
                return PC_ERR_CORRUPT;
            }
        }

        {
            uint16_t target = dec->comp_len == 0u ? dec->raw_len : dec->comp_len;
            size_t need = (size_t)(target - dec->payload_len);
            size_t take = len - pos;
            if (take > need) {
                take = need;
            }
            memcpy(dec->payload + dec->payload_len, data + pos, take);
            dec->payload_len = (uint16_t)(dec->payload_len + (uint16_t)take);
            pos += take;

            if (dec->payload_len == target) {
                pc_result rc = pc_decoder_emit_block(dec, write_fn, user);
                if (rc != PC_OK) {
                    return rc;
                }
                dec->header_len = 0u;
                dec->raw_len = 0u;
                dec->comp_len = 0u;
                dec->payload_len = 0u;
            }
        }
    }

    return PC_OK;
}

pc_result pc_decoder_finish(pc_decoder *dec) {
    if (dec == NULL) {
        return PC_ERR_INPUT;
    }
    if (dec->header_len != 0u || dec->raw_len != 0u || dec->comp_len != 0u || dec->payload_len != 0u) {
        return PC_ERR_CORRUPT;
    }
    return PC_OK;
}

size_t pc_compress_bound(size_t input_len) {
    size_t blocks;
    if (input_len == 0u) {
        return 0u;
    }
    blocks = (input_len + (size_t)PC_BLOCK_SIZE - 1u) / (size_t)PC_BLOCK_SIZE;
    return input_len + (blocks * 4u);
}

typedef struct pc_mem_writer {
    uint8_t *out;
    size_t cap;
    size_t len;
} pc_mem_writer;

static int pc_mem_write(void *user, const uint8_t *data, size_t len) {
    pc_mem_writer *w = (pc_mem_writer *)user;
    if (w->len + len > w->cap) {
        return 1;
    }
    memcpy(w->out + w->len, data, len);
    w->len += len;
    return 0;
}

pc_result pc_compress_buffer(
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_cap,
    size_t *output_len
) {
    pc_encoder enc;
    pc_mem_writer writer;
    pc_result rc;

    if ((input_len > 0u && input == NULL) || output == NULL || output_len == NULL) {
        return PC_ERR_INPUT;
    }

    writer.out = output;
    writer.cap = output_cap;
    writer.len = 0u;

    pc_encoder_init(&enc);
    rc = pc_encoder_sink(&enc, input, input_len, pc_mem_write, &writer);
    if (rc != PC_OK) {
        return rc == PC_ERR_WRITE ? PC_ERR_OUTPUT_TOO_SMALL : rc;
    }
    rc = pc_encoder_finish(&enc, pc_mem_write, &writer);
    if (rc != PC_OK) {
        return rc == PC_ERR_WRITE ? PC_ERR_OUTPUT_TOO_SMALL : rc;
    }

    *output_len = writer.len;
    return PC_OK;
}

pc_result pc_decompress_buffer(
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_cap,
    size_t *output_len
) {
    pc_decoder dec;
    pc_mem_writer writer;
    pc_result rc;

    if ((input_len > 0u && input == NULL) || output == NULL || output_len == NULL) {
        return PC_ERR_INPUT;
    }

    writer.out = output;
    writer.cap = output_cap;
    writer.len = 0u;

    pc_decoder_init(&dec);
    rc = pc_decoder_sink(&dec, input, input_len, pc_mem_write, &writer);
    if (rc != PC_OK) {
        return rc == PC_ERR_WRITE ? PC_ERR_OUTPUT_TOO_SMALL : rc;
    }
    rc = pc_decoder_finish(&dec);
    if (rc != PC_OK) {
        return rc;
    }

    *output_len = writer.len;
    return PC_OK;
}
