#include "picocompress.h"

#include <stdio.h>
#include <string.h>

static uint32_t pc_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    for (i = 0; i < len; ++i) {
        uint32_t byte = data[i];
        int j;
        crc ^= byte;
        for (j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

typedef struct buffer_writer {
    uint8_t *data;
    size_t cap;
    size_t len;
} buffer_writer;

static int write_to_buffer(void *user, const uint8_t *data, size_t len) {
    buffer_writer *w = (buffer_writer *)user;
    if (w->len + len > w->cap) {
        return 1;
    }
    memcpy(w->data + w->len, data, len);
    w->len += len;
    return 0;
}

int main(void) {
    enum { INPUT_SIZE = 4096 };
    enum { COMPRESSED_CAP = INPUT_SIZE + (((INPUT_SIZE + PC_BLOCK_SIZE - 1u) / PC_BLOCK_SIZE) * 4u) + 32u };
    uint8_t input[INPUT_SIZE];
    uint8_t compressed[COMPRESSED_CAP];
    uint8_t restored[INPUT_SIZE];
    size_t compressed_len = 0;
    size_t restored_len = 0;
    size_t i;

    for (i = 0; i < sizeof(input); ++i) {
        if ((i % 64u) < 52u) {
            static const char pat[] = "AACCBBDDAACCBBDD1122334455667788";
            input[i] = (uint8_t)pat[i % (sizeof(pat) - 1u)];
        } else {
            input[i] = (uint8_t)(i * 17u);
        }
    }

    if (pc_compress_buffer(input, sizeof(input), compressed, sizeof(compressed), &compressed_len) != PC_OK) {
        fprintf(stderr, "pc_compress_buffer failed\n");
        return 1;
    }

    if (pc_decompress_buffer(compressed, compressed_len, restored, sizeof(restored), &restored_len) != PC_OK) {
        fprintf(stderr, "pc_decompress_buffer failed\n");
        return 1;
    }
    if (restored_len != sizeof(input) || memcmp(input, restored, sizeof(input)) != 0) {
        fprintf(stderr, "one-shot roundtrip mismatch\n");
        return 1;
    }
    {
        uint32_t crc_in = pc_crc32(input, sizeof(input));
        uint32_t crc_out = pc_crc32(restored, restored_len);
        if (crc_in != crc_out) {
            fprintf(stderr, "one-shot CRC32 mismatch: 0x%08X vs 0x%08X\n", crc_in, crc_out);
            return 1;
        }
    }

    {
        pc_encoder enc;
        pc_decoder dec;
        uint8_t compressed_stream[sizeof(compressed)];
        uint8_t restored_stream[sizeof(restored)];
        buffer_writer cw;
        buffer_writer rw;
        size_t pos;

        cw.data = compressed_stream;
        cw.cap = sizeof(compressed_stream);
        cw.len = 0;
        rw.data = restored_stream;
        rw.cap = sizeof(restored_stream);
        rw.len = 0;

        pc_encoder_init(&enc);
        pos = 0;
        while (pos < sizeof(input)) {
            size_t step = (pos % 31u) + 1u;
            if (step > sizeof(input) - pos) {
                step = sizeof(input) - pos;
            }
            if (pc_encoder_sink(&enc, input + pos, step, write_to_buffer, &cw) != PC_OK) {
                fprintf(stderr, "pc_encoder_sink failed\n");
                return 1;
            }
            pos += step;
        }
        if (pc_encoder_finish(&enc, write_to_buffer, &cw) != PC_OK) {
            fprintf(stderr, "pc_encoder_finish failed\n");
            return 1;
        }

        pc_decoder_init(&dec);
        pos = 0;
        while (pos < cw.len) {
            size_t step = (pos % 23u) + 1u;
            if (step > cw.len - pos) {
                step = cw.len - pos;
            }
            if (pc_decoder_sink(&dec, cw.data + pos, step, write_to_buffer, &rw) != PC_OK) {
                fprintf(stderr, "pc_decoder_sink failed\n");
                return 1;
            }
            pos += step;
        }
        if (pc_decoder_finish(&dec) != PC_OK) {
            fprintf(stderr, "pc_decoder_finish failed\n");
            return 1;
        }

        if (rw.len != sizeof(input) || memcmp(input, rw.data, sizeof(input)) != 0) {
            fprintf(stderr, "streaming roundtrip mismatch\n");
            return 1;
        }
        {
            uint32_t crc_in = pc_crc32(input, sizeof(input));
            uint32_t crc_out = pc_crc32(rw.data, rw.len);
            if (crc_in != crc_out) {
                fprintf(stderr, "streaming CRC32 mismatch: 0x%08X vs 0x%08X\n", crc_in, crc_out);
                return 1;
            }
        }
    }

    printf(
        "ok: %zu bytes -> %zu bytes (%.2fx)\n",
        sizeof(input),
        compressed_len,
        compressed_len > 0 ? (double)sizeof(input) / (double)compressed_len : 0.0
    );
    return 0;
}
