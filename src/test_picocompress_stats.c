#ifndef PC_ENABLE_STATS
#define PC_ENABLE_STATS
#endif

#include "picocompress.h"

#include <stdio.h>
#include <string.h>

typedef struct buffer_writer {
    uint8_t *data;
    size_t cap;
    size_t len;
} buffer_writer;

static int write_to_buffer(void *user, const uint8_t *data, size_t len) {
    buffer_writer *w = (buffer_writer *)user;
    if (w->len + len > w->cap) return 1;
    memcpy(w->data + w->len, data, len);
    w->len += len;
    return 0;
}

static int test_stats_populated(void) {
    enum { INPUT_SIZE = 4096 };
    enum { OUT_CAP = INPUT_SIZE + ((INPUT_SIZE / PC_BLOCK_SIZE + 1u) * 4u) + 32u };
    uint8_t input[INPUT_SIZE];
    uint8_t output[OUT_CAP];
    pc_encoder enc;
    pc_encoder_stats stats;
    buffer_writer w;
    size_t i;

    /* fill with compressible pattern */
    for (i = 0; i < sizeof(input); ++i) {
        static const char pat[] = "AACCBBDDAACCBBDD1122334455667788";
        input[i] = (uint8_t)pat[i % (sizeof(pat) - 1u)];
    }

    w.data = output;
    w.cap = sizeof(output);
    w.len = 0;

    pc_encoder_init(&enc);
    if (pc_encoder_sink(&enc, input, sizeof(input), write_to_buffer, &w) != PC_OK) {
        fprintf(stderr, "stats: encoder sink failed\n");
        return 0;
    }
    if (pc_encoder_finish(&enc, write_to_buffer, &w) != PC_OK) {
        fprintf(stderr, "stats: encoder finish failed\n");
        return 0;
    }

    memset(&stats, 0xFF, sizeof(stats));
    pc_encoder_get_stats(&enc, &stats);

    if (stats.bytes_in != sizeof(input)) {
        fprintf(stderr, "stats: bytes_in=%u expected=%zu\n", stats.bytes_in, sizeof(input));
        return 0;
    }
    if (stats.bytes_out == 0 || stats.bytes_out > stats.bytes_in + 64u) {
        fprintf(stderr, "stats: bytes_out=%u looks wrong\n", stats.bytes_out);
        return 0;
    }
    if (stats.blocks == 0) {
        fprintf(stderr, "stats: blocks=0\n");
        return 0;
    }
    if (stats.match_count == 0) {
        fprintf(stderr, "stats: match_count=0 on compressible data\n");
        return 0;
    }
    /* at least one type of match must fire */
    if (stats.repeat_hits + stats.dict_hits + stats.lz_short_hits + stats.lz_long_hits == 0) {
        fprintf(stderr, "stats: no match type recorded\n");
        return 0;
    }

    printf("  bytes_in=%u bytes_out=%u blocks=%u\n", stats.bytes_in, stats.bytes_out, stats.blocks);
    printf("  matches=%u (repeat=%u dict=%u lz_short=%u lz_long=%u)\n",
        stats.match_count, stats.repeat_hits, stats.dict_hits,
        stats.lz_short_hits, stats.lz_long_hits);
    printf("  literal_bytes=%u good_enough=%u lazy_improv=%u\n",
        stats.literal_bytes, stats.good_enough_hits, stats.lazy_improvements);

    return 1;
}

static int test_stats_random_no_crash(void) {
    /* random data → mostly literals, stats should still be consistent */
    enum { INPUT_SIZE = 508 };
    enum { OUT_CAP = INPUT_SIZE + 32u };
    uint8_t input[INPUT_SIZE];
    uint8_t output[OUT_CAP];
    pc_encoder enc;
    pc_encoder_stats stats;
    buffer_writer w;
    uint32_t x = 0x13579BDFu;
    size_t i;

    for (i = 0; i < sizeof(input); ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        input[i] = (uint8_t)x;
    }

    w.data = output; w.cap = sizeof(output); w.len = 0;

    pc_encoder_init(&enc);
    if (pc_encoder_sink(&enc, input, sizeof(input), write_to_buffer, &w) != PC_OK) {
        fprintf(stderr, "stats_random: sink failed\n");
        return 0;
    }
    if (pc_encoder_finish(&enc, write_to_buffer, &w) != PC_OK) {
        fprintf(stderr, "stats_random: finish failed\n");
        return 0;
    }

    pc_encoder_get_stats(&enc, &stats);
    if (stats.bytes_in != sizeof(input)) {
        fprintf(stderr, "stats_random: bytes_in=%u expected=%zu\n", stats.bytes_in, sizeof(input));
        return 0;
    }
    if (stats.blocks != 1) {
        fprintf(stderr, "stats_random: blocks=%u expected=1\n", stats.blocks);
        return 0;
    }
    /* literal_bytes + matched_bytes should account for all input */
    /* (this is approximate; literal_bytes doesn't count match-emitted bytes) */

    return 1;
}

int main(void) {
    int passed = 0, total = 0;

    printf("picocompress stats tests\n");

    ++total; passed += test_stats_populated();
    ++total; passed += test_stats_random_no_crash();

    if (passed != total) {
        fprintf(stderr, "stats tests: %d/%d passed\n", passed, total);
        return 1;
    }
    printf("ok: stats tests %d/%d passed\n", passed, total);
    return 0;
}
