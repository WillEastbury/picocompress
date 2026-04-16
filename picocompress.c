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

static uint16_t pc_compress_block(
    const uint8_t *in,
    uint16_t in_len,
    uint8_t *out,
    uint16_t out_cap
) {
    int16_t head[PC_HASH_CHAIN_DEPTH][PC_HASH_SIZE];
    uint16_t i;
    uint16_t anchor = 0;
    uint16_t op = 0;
    memset(head, 0xFF, sizeof(head));

    i = 0;
    while ((uint16_t)(i + PC_MATCH_MIN) <= in_len) {
        uint16_t match_len = 0;
        uint16_t match_off = 0;
        uint16_t hash = pc_hash3(in + i);
        uint16_t max_len = (uint16_t)(in_len - i);
        int d;

        if (max_len > PC_MATCH_MAX) {
            max_len = PC_MATCH_MAX;
        }

        if (max_len >= PC_MATCH_MIN) {
            for (d = 0; d < (int)PC_HASH_CHAIN_DEPTH; ++d) {
                int16_t prev = head[d][hash];
                uint16_t off;
                uint16_t prev_pos;
                uint16_t len;

                if (prev < 0) {
                    continue;
                }

                prev_pos = (uint16_t)prev;
                off = (uint16_t)(i - prev_pos);
                if (off == 0u || off > PC_OFFSET_MAX) {
                    continue;
                }

                len = pc_match_len(in + prev_pos, in + i, max_len);
                if (len > match_len) {
                    match_len = len;
                    match_off = off;
                    if (match_len == max_len) {
                        break;
                    }
                }
            }
        }

        pc_head_insert(head, hash, (int16_t)i);

        if (match_len >= PC_MATCH_MIN && (uint16_t)(i + PC_MATCH_MIN + 1u) <= in_len) {
            uint16_t next_pos = (uint16_t)(i + 1u);
            uint16_t next_max = (uint16_t)(in_len - next_pos);
            uint16_t next_len = 0;
            if (next_max > PC_MATCH_MAX) {
                next_max = PC_MATCH_MAX;
            }
            if (next_max >= PC_MATCH_MIN) {
                uint16_t next_hash = pc_hash3(in + next_pos);
                for (d = 0; d < (int)PC_HASH_CHAIN_DEPTH; ++d) {
                    int16_t next_prev = head[d][next_hash];
                    uint16_t prev_pos;
                    uint16_t next_off;
                    uint16_t len;

                    if (next_prev < 0) {
                        continue;
                    }

                    prev_pos = (uint16_t)next_prev;
                    next_off = (uint16_t)(next_pos - prev_pos);
                    if (next_off == 0u || next_off > PC_OFFSET_MAX) {
                        continue;
                    }

                    len = pc_match_len(in + prev_pos, in + next_pos, next_max);
                    if (len > next_len) {
                        next_len = len;
                        if (next_len == next_max) {
                            break;
                        }
                    }
                }
            }

            if (next_len >= (uint16_t)(match_len + 1u)) {
                ++i;
                continue;
            }
        }

        if (match_len >= PC_MATCH_MIN) {
            uint16_t lit_len = (uint16_t)(i - anchor);
            uint16_t k;

            if (!pc_emit_literals(in + anchor, lit_len, out, out_cap, &op)) {
                return UINT16_MAX;
            }
            if ((uint32_t)op + 2u > out_cap) {
                return UINT16_MAX;
            }

            out[op++] = (uint8_t)(
                0x80u
                | ((uint8_t)(match_len - PC_MATCH_MIN) << 1u)
                | ((match_off >> 8u) & 0x01u)
            );
            out[op++] = (uint8_t)(match_off & 0xFFu);

            for (k = 1; k < match_len && (uint16_t)(i + k + 2u) < in_len; ++k) {
                pc_head_insert(head, pc_hash3(in + i + k), (int16_t)(i + k));
            }

            i = (uint16_t)(i + match_len);
            anchor = i;
        } else {
            ++i;
        }
    }

    if (anchor < in_len) {
        if (!pc_emit_literals(in + anchor, (uint16_t)(in_len - anchor), out, out_cap, &op)) {
            return UINT16_MAX;
        }
    }

    return op;
}

static pc_result pc_decompress_block(
    const uint8_t *in,
    uint16_t in_len,
    uint8_t *out,
    uint16_t out_len
) {
    uint16_t ip = 0;
    uint16_t op = 0;

    while (ip < in_len) {
        uint8_t token = in[ip++];
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

        if (ip >= in_len) {
            return PC_ERR_CORRUPT;
        }
        {
            uint16_t match_len = (uint16_t)(((token >> 1u) & ((1u << PC_MATCH_CODE_BITS) - 1u)) + PC_MATCH_MIN);
            uint16_t off = (uint16_t)(((uint16_t)(token & 0x01u) << 8u) | (uint16_t)in[ip++]);
            uint16_t src;
            uint16_t j;

            if (off == 0u || off > op) {
                return PC_ERR_CORRUPT;
            }
            if ((uint32_t)op + match_len > out_len) {
                return PC_ERR_CORRUPT;
            }

            src = (uint16_t)(op - off);
            for (j = 0; j < match_len; ++j) {
                out[op++] = out[src + j];
            }
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

static pc_result pc_encoder_flush(pc_encoder *enc, pc_write_fn write_fn, void *user) {
    uint8_t tmp[PC_BLOCK_MAX_COMPRESSED];
    uint8_t header[4];
    uint16_t raw_len;
    uint16_t comp_len;
    pc_result rc;

    if (enc->block_len == 0u) {
        return PC_OK;
    }

    raw_len = enc->block_len;
    comp_len = pc_compress_block(enc->block, raw_len, tmp, (uint16_t)sizeof(tmp));

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
        return pc_write_all(write_fn, user, dec->payload, dec->raw_len);
    }
    {
        pc_result rc = pc_decompress_block(dec->payload, dec->comp_len, dec->raw, dec->raw_len);
        if (rc != PC_OK) {
            return rc;
        }
        return pc_write_all(write_fn, user, dec->raw, dec->raw_len);
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
