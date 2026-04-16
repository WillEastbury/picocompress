#include "picocompress.h"

#include <string.h>

#define PC_HASH_SIZE (1u << PC_HASH_BITS)
#define PC_INVALID_POS (-1)

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
        if (chunk > PC_LITERAL_MAX) {
            chunk = PC_LITERAL_MAX;
        }
        if ((uint32_t)(*op) + 1u + chunk > dst_cap) {
            return 0;
        }
        dst[(*op)++] = (uint8_t)(chunk - 1u);
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

/* ---- general-purpose static dictionary (32 entries, ROM/flash) ----
 *  0- 3: single-byte ultra-common symbols (JSON/CSV/code)
 *  4- 7: two-byte common pairs
 *  8-11: three-byte patterns
 * 12-19: four-byte common words/fields
 * 20-25: five-six byte common words
 * 26-31: long high-value entries                                     */

/* 0-3: single-byte symbols */
static const uint8_t pc_d00[] = "{";
static const uint8_t pc_d01[] = "}";
static const uint8_t pc_d02[] = ":";
static const uint8_t pc_d03[] = ",";
/* 4-7: two-byte pairs */
static const uint8_t pc_d04[] = { '\r', '\n' };
static const uint8_t pc_d05[] = "id";
static const uint8_t pc_d06[] = { '"', ':' };              /* ":  JSON key→value */
static const uint8_t pc_d07[] = { ',', '"' };              /* ,"  JSON separator */
/* 8-11: three-byte */
static const uint8_t pc_d08[] = "000";
static const uint8_t pc_d09[] = "ORD";
static const uint8_t pc_d10[] = "the";
static const uint8_t pc_d11[] = "ing";
/* 12-19: four-byte words/fields */
static const uint8_t pc_d12[] = { 'n','o','"',':' };       /* no":  */
static const uint8_t pc_d13[] = "true";
static const uint8_t pc_d14[] = "null";
static const uint8_t pc_d15[] = "name";
static const uint8_t pc_d16[] = "data";
static const uint8_t pc_d17[] = "time";
static const uint8_t pc_d18[] = "type";
static const uint8_t pc_d19[] = "mode";
/* 20-25: five-six byte */
static const uint8_t pc_d20[] = "false";
static const uint8_t pc_d21[] = "error";
static const uint8_t pc_d22[] = "value";
static const uint8_t pc_d23[] = "state";
static const uint8_t pc_d24[] = "status";
static const uint8_t pc_d25[] = "number";
/* 26-31: long high-value entries */
static const uint8_t pc_d26[] = "active";
static const uint8_t pc_d27[] = "device";
static const uint8_t pc_d28[] = "message";
static const uint8_t pc_d29[] = { 'n','u','m','b','e','r','"',':' }; /* number": */
static const uint8_t pc_d30[] = "operator";
static const uint8_t pc_d31[] = "https://";

static const pc_dict_entry_t pc_static_dict[PC_DICT_COUNT] = {
    { pc_d00, 1 }, { pc_d01, 1 }, { pc_d02, 1 }, { pc_d03, 1 },
    { pc_d04, 2 }, { pc_d05, 2 }, { pc_d06, 2 }, { pc_d07, 2 },
    { pc_d08, 3 }, { pc_d09, 3 }, { pc_d10, 3 }, { pc_d11, 3 },
    { pc_d12, 4 }, { pc_d13, 4 }, { pc_d14, 4 }, { pc_d15, 4 },
    { pc_d16, 4 }, { pc_d17, 4 }, { pc_d18, 4 }, { pc_d19, 4 },
    { pc_d20, 5 }, { pc_d21, 5 }, { pc_d22, 5 }, { pc_d23, 5 },
    { pc_d24, 6 }, { pc_d25, 6 }, { pc_d26, 6 }, { pc_d27, 6 },
    { pc_d28, 7 }, { pc_d29, 8 }, { pc_d30, 8 }, { pc_d31, 8 },
};

/* Find best savings among dict, LZ, and repeat-offset at a virtual position.
 * vbuf = [history | block], vpos is the current virtual position.
 * Returns net savings (bytes saved vs literal). Fills out_* params. */
static int pc_find_best(
    const uint8_t *vbuf, uint16_t vbuf_len, uint16_t vpos,
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE],
    uint16_t last_offset,
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

    /* dictionary match (1-byte token -> savings = len - 1) */
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
        }
    }

    /* LZ match -- needs 3 bytes for hash */
    if (remaining >= 3u) {
        uint16_t hash = pc_hash3(vbuf + vpos);
        uint16_t max_len = remaining > PC_MATCH_MAX ? PC_MATCH_MAX : remaining;

        for (d = 0; d < (int)PC_HASH_CHAIN_DEPTH; ++d) {
            int16_t prev = head[d][hash];
            uint16_t prev_pos, off, len;
            int is_rep, s;

            if (prev < 0) continue;
            prev_pos = (uint16_t)prev;
            if (prev_pos >= vpos) continue;
            off = (uint16_t)(vpos - prev_pos);
            if (off > PC_OFFSET_MAX) continue;

            len = pc_match_len(vbuf + prev_pos, vbuf + vpos, max_len);
            if (len < PC_MATCH_MIN) continue;

            is_rep = (off == last_offset && last_offset != 0) ? 1 : 0;
            s = is_rep ? (int)len - 1 : (int)len - 2;

            if (s > best_savings || (s == best_savings && len > *out_len)) {
                best_savings = s;
                *out_len = len;
                *out_off = off;
                *out_dict = UINT16_MAX;
                *out_is_repeat = is_rep;
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
    uint16_t vbuf_len = (uint16_t)(hist_len + block_len);
    uint16_t vpos;
    uint16_t anchor;
    uint16_t op = 0;
    uint16_t last_offset = 0;
    memset(head, 0xFF, sizeof(head));

    /* seed hash table from history so cross-block matches are found */
    if (hist_len >= 3u) {
        uint16_t p;
        for (p = 0; (uint16_t)(p + 2u) < hist_len; ++p) {
            pc_head_insert(head, pc_hash3(vbuf + p), (int16_t)p);
        }
    }

    anchor = hist_len;
    vpos = hist_len;
    while (vpos < vbuf_len) {
        uint16_t best_len = 0, best_off = 0, best_dict = UINT16_MAX;
        int best_is_repeat = 0;
        int best_savings;

        if ((uint16_t)(vbuf_len - vpos) < PC_MATCH_MIN) {
            break;
        }

        best_savings = pc_find_best(
            vbuf, vbuf_len, vpos, head, last_offset,
            &best_len, &best_off, &best_dict, &best_is_repeat);

        /* insert current position into hash table (needs 3 bytes) */
        if ((uint16_t)(vbuf_len - vpos) >= 3u) {
            pc_head_insert(head, pc_hash3(vbuf + vpos), (int16_t)vpos);
        }

        /* short dict entries save a literal-header byte when standalone */
        if (best_dict != UINT16_MAX && best_savings == 0 && anchor == vpos) {
            best_savings = 1;
        }

        /* lazy matching: if next position is better, skip current */
        if (best_savings > 0 && (uint16_t)(vpos + 1u) < vbuf_len
            && (uint16_t)(vbuf_len - vpos - 1u) >= PC_MATCH_MIN) {
            uint16_t n_len, n_off, n_dict;
            int n_rep;
            int n_sav = pc_find_best(
                vbuf, vbuf_len, (uint16_t)(vpos + 1u), head, last_offset,
                &n_len, &n_off, &n_dict, &n_rep);
            if (n_sav > best_savings) {
                ++vpos;
                continue;
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
                out[op++] = (uint8_t)(0xE0u | (best_dict & 0x1Fu));
            } else if (best_is_repeat) {
                if ((uint32_t)op + 1u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(0xC0u | ((best_len - PC_MATCH_MIN) & 0x1Fu));
            } else {
                if ((uint32_t)op + 2u > out_cap) return UINT16_MAX;
                out[op++] = (uint8_t)(
                    0x80u
                    | (((best_len - PC_MATCH_MIN) & 0x1Fu) << 1u)
                    | ((best_off >> 8u) & 0x01u));
                out[op++] = (uint8_t)(best_off & 0xFFu);
                last_offset = best_off;
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

        /* 0x00..0x7F: literal run */
        if ((token & 0x80u) == 0u) {
            uint16_t lit_len = (uint16_t)((token & 0x7Fu) + 1u);
            if ((uint32_t)ip + lit_len > in_len || (uint32_t)op + lit_len > out_len) {
                return PC_ERR_CORRUPT;
            }
            memcpy(out + op, in + ip, lit_len);
            ip = (uint16_t)(ip + lit_len);
            op = (uint16_t)(op + lit_len);
            continue;
        }

        /* 0xE0..0xFF: dictionary reference */
        if ((token & 0xE0u) == 0xE0u) {
            uint16_t idx = (uint16_t)(token & 0x1Fu);
            uint8_t dlen;
            if (idx >= PC_DICT_COUNT) return PC_ERR_CORRUPT;
            dlen = pc_static_dict[idx].len;
            if ((uint32_t)op + dlen > out_len) return PC_ERR_CORRUPT;
            memcpy(out + op, pc_static_dict[idx].data, dlen);
            op = (uint16_t)(op + dlen);
            continue;
        }

        /* 0xC0..0xDF: repeat-offset match */
        if ((token & 0xE0u) == 0xC0u) {
            uint16_t match_len = (uint16_t)((token & 0x1Fu) + PC_MATCH_MIN);
            if (last_offset == 0u) return PC_ERR_CORRUPT;
            if (last_offset > (uint16_t)(op + hist_len)) return PC_ERR_CORRUPT;
            if ((uint32_t)op + match_len > out_len) return PC_ERR_CORRUPT;
            pc_copy_match(out, &op, hist, hist_len, last_offset, match_len);
            continue;
        }

        /* 0x80..0xBF: LZ match with explicit offset */
        if (ip >= in_len) return PC_ERR_CORRUPT;
        {
            uint16_t match_len = (uint16_t)(((token >> 1u) & 0x1Fu) + PC_MATCH_MIN);
            uint16_t off = (uint16_t)(((uint16_t)(token & 0x01u) << 8u) | (uint16_t)in[ip++]);

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
