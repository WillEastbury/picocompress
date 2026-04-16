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

enum { PAYLOAD_508 = 508 };
enum {
    COMPRESSED_508_CAP =
        PAYLOAD_508 + (((PAYLOAD_508 + PC_BLOCK_SIZE - 1u) / PC_BLOCK_SIZE) * 4u) + 16u
};

static int write_to_buffer(void *user, const uint8_t *data, size_t len) {
    buffer_writer *w = (buffer_writer *)user;
    if (w->len + len > w->cap) {
        return 1;
    }
    memcpy(w->data + w->len, data, len);
    w->len += len;
    return 0;
}

static void fill_random(uint8_t *dst, size_t len) {
    uint32_t x = 0x13579BDFu;
    size_t i;
    for (i = 0; i < len; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        dst[i] = (uint8_t)x;
    }
}

static int test_zero_length(void) {
    uint8_t compressed[8];
    uint8_t restored[8];
    size_t compressed_len = 0;
    size_t restored_len = 0;
    pc_result rc;

    rc = pc_compress_buffer(NULL, 0, compressed, sizeof(compressed), &compressed_len);
    if (rc != PC_OK || compressed_len != 0) {
        fprintf(stderr, "zero-length compress failed\n");
        return 0;
    }

    rc = pc_decompress_buffer(NULL, 0, restored, sizeof(restored), &restored_len);
    if (rc != PC_OK || restored_len != 0) {
        fprintf(stderr, "zero-length decompress failed\n");
        return 0;
    }

    return 1;
}

static int test_raw_fallback(void) {
    uint8_t input[PAYLOAD_508];
    uint8_t compressed[COMPRESSED_508_CAP];
    uint8_t restored[sizeof(input)];
    size_t compressed_len = 0;
    size_t restored_len = 0;
    pc_result rc;

    fill_random(input, sizeof(input));

    rc = pc_compress_buffer(input, sizeof(input), compressed, sizeof(compressed), &compressed_len);
    if (rc != PC_OK) {
        fprintf(stderr, "raw fallback compress failed\n");
        return 0;
    }
    if (compressed_len != (sizeof(input) + 4u) || compressed[2] != 0u || compressed[3] != 0u) {
        fprintf(stderr, "raw fallback header check failed\n");
        return 0;
    }

    rc = pc_decompress_buffer(compressed, compressed_len, restored, sizeof(restored), &restored_len);
    if (rc != PC_OK || restored_len != sizeof(input) || memcmp(input, restored, sizeof(input)) != 0) {
        fprintf(stderr, "raw fallback roundtrip failed\n");
        return 0;
    }
    if (pc_crc32(input, sizeof(input)) != pc_crc32(restored, restored_len)) {
        fprintf(stderr, "raw fallback CRC32 mismatch\n");
        return 0;
    }

    return 1;
}

static int test_output_too_small(void) {
    uint8_t input[PAYLOAD_508];
    uint8_t tiny_out[64];
    uint8_t compressed[COMPRESSED_508_CAP];
    uint8_t tiny_restore[507];
    size_t compressed_len = 0;
    size_t out_len = 0;
    pc_result rc;

    fill_random(input, sizeof(input));

    rc = pc_compress_buffer(input, sizeof(input), tiny_out, sizeof(tiny_out), &out_len);
    if (rc != PC_ERR_OUTPUT_TOO_SMALL) {
        fprintf(stderr, "compress small-output check failed\n");
        return 0;
    }

    rc = pc_compress_buffer(input, sizeof(input), compressed, sizeof(compressed), &compressed_len);
    if (rc != PC_OK) {
        fprintf(stderr, "compress for small-decode check failed\n");
        return 0;
    }

    rc = pc_decompress_buffer(compressed, compressed_len, tiny_restore, sizeof(tiny_restore), &out_len);
    if (rc != PC_ERR_OUTPUT_TOO_SMALL) {
        fprintf(stderr, "decompress small-output check failed\n");
        return 0;
    }

    return 1;
}

static int test_corrupt_stream(void) {
    uint8_t bad_stream[] = { 0x03, 0x00, 0x02, 0x00, 0x80, 0x01 };
    uint8_t out[16];
    size_t out_len = 0;
    pc_result rc = pc_decompress_buffer(bad_stream, sizeof(bad_stream), out, sizeof(out), &out_len);
    if (rc != PC_ERR_CORRUPT) {
        fprintf(stderr, "corrupt stream detection failed\n");
        return 0;
    }
    return 1;
}

static int test_streaming_roundtrip_508(void) {
    uint8_t input[PAYLOAD_508];
    uint8_t compressed[COMPRESSED_508_CAP];
    uint8_t restored[sizeof(input)];
    buffer_writer cw;
    buffer_writer rw;
    pc_encoder enc;
    pc_decoder dec;
    size_t pos;

    fill_random(input, sizeof(input));

    cw.data = compressed;
    cw.cap = sizeof(compressed);
    cw.len = 0;
    rw.data = restored;
    rw.cap = sizeof(restored);
    rw.len = 0;

    pc_encoder_init(&enc);
    pos = 0;
    while (pos < sizeof(input)) {
        size_t step = (pos % 17u) + 1u;
        if (step > sizeof(input) - pos) {
            step = sizeof(input) - pos;
        }
        if (pc_encoder_sink(&enc, input + pos, step, write_to_buffer, &cw) != PC_OK) {
            fprintf(stderr, "streaming encode sink failed\n");
            return 0;
        }
        pos += step;
    }
    if (pc_encoder_finish(&enc, write_to_buffer, &cw) != PC_OK) {
        fprintf(stderr, "streaming encode finish failed\n");
        return 0;
    }

    pc_decoder_init(&dec);
    pos = 0;
    while (pos < cw.len) {
        size_t step = (pos % 19u) + 1u;
        if (step > cw.len - pos) {
            step = cw.len - pos;
        }
        if (pc_decoder_sink(&dec, cw.data + pos, step, write_to_buffer, &rw) != PC_OK) {
            fprintf(stderr, "streaming decode sink failed\n");
            return 0;
        }
        pos += step;
    }
    if (pc_decoder_finish(&dec) != PC_OK) {
        fprintf(stderr, "streaming decode finish failed\n");
        return 0;
    }

    if (rw.len != sizeof(input) || memcmp(input, rw.data, sizeof(input)) != 0) {
        fprintf(stderr, "streaming 508 roundtrip mismatch\n");
        return 0;
    }
    if (pc_crc32(input, sizeof(input)) != pc_crc32(rw.data, rw.len)) {
        fprintf(stderr, "streaming 508 CRC32 mismatch\n");
        return 0;
    }

    return 1;
}

int main(void) {
    int passed = 0;
    int total = 0;

    ++total; passed += test_zero_length();
    ++total; passed += test_raw_fallback();
    ++total; passed += test_output_too_small();
    ++total; passed += test_corrupt_stream();
    ++total; passed += test_streaming_roundtrip_508();

    if (passed != total) {
        fprintf(stderr, "additional tests: %d/%d passed\n", passed, total);
        return 1;
    }

    printf("ok: additional tests %d/%d passed\n", passed, total);
    return 0;
}
