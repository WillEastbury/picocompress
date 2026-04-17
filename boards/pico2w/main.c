/*
 * picocompress multi-profile benchmark — Pico 2W (RP2350, Cortex-M33)
 * All 5 profiles + heatshrink w11 comparison.
 * Output goes to USB serial (stdio_usb).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"

/* ------------------------------------------------------------------ */
/* Forward-declare each profile's buffer API (defined in pc_*.c)      */
/* ------------------------------------------------------------------ */
typedef int pc_result_t;
pc_result_t pc_micro_compress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_micro_decompress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_min_compress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_min_decompress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_bal_compress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_bal_decompress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_q3_compress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_q3_decompress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_q4_compress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);
pc_result_t pc_q4_decompress_buffer(const uint8_t*, size_t, uint8_t*, size_t, size_t*);

/* ------------------------------------------------------------------ */
/* heatshrink w11 l4 no-index                                         */
/* ------------------------------------------------------------------ */
#include "heatshrink_encoder.h"
#include "heatshrink_decoder.h"

#define HS_WINDOW   11
#define HS_LOOKAHEAD 4
#define HS_DEC_BUF  256

static int hs_compress(const uint8_t *in, size_t in_len,
                       uint8_t *out, size_t out_cap, size_t *out_len) {
    heatshrink_encoder *enc = heatshrink_encoder_alloc(HS_WINDOW, HS_LOOKAHEAD);
    if (!enc) return -1;
    heatshrink_encoder_reset(enc);
    size_t sunk = 0, polled = 0, poll_sz;
    while (sunk < in_len) {
        size_t n = 0;
        heatshrink_encoder_sink(enc, (uint8_t *)in + sunk, in_len - sunk, &n);
        sunk += n;
        do { poll_sz = 0;
             heatshrink_encoder_poll(enc, out + polled, out_cap - polled, &poll_sz);
             polled += poll_sz;
        } while (poll_sz > 0);
    }
    while (heatshrink_encoder_finish(enc) == HSER_FINISH_MORE) {
        do { poll_sz = 0;
             heatshrink_encoder_poll(enc, out + polled, out_cap - polled, &poll_sz);
             polled += poll_sz;
        } while (poll_sz > 0);
    }
    *out_len = polled;
    heatshrink_encoder_free(enc);
    return 0;
}

static int hs_decompress(const uint8_t *in, size_t in_len,
                          uint8_t *out, size_t out_cap, size_t *out_len) {
    heatshrink_decoder *dec = heatshrink_decoder_alloc(HS_DEC_BUF, HS_WINDOW, HS_LOOKAHEAD);
    if (!dec) return -1;
    heatshrink_decoder_reset(dec);
    size_t sunk = 0, polled = 0, poll_sz;
    while (sunk < in_len) {
        size_t n = 0;
        heatshrink_decoder_sink(dec, (uint8_t *)in + sunk, in_len - sunk, &n);
        sunk += n;
        do { poll_sz = 0;
             heatshrink_decoder_poll(dec, out + polled, out_cap - polled, &poll_sz);
             polled += poll_sz;
        } while (poll_sz > 0);
    }
    while (heatshrink_decoder_finish(dec) == HSDR_FINISH_MORE) {
        do { poll_sz = 0;
             heatshrink_decoder_poll(dec, out + polled, out_cap - polled, &poll_sz);
             polled += poll_sz;
        } while (poll_sz > 0);
    }
    *out_len = polled;
    heatshrink_decoder_free(dec);
    return 0;
}

/* ------------------------------------------------------------------ */
/* CRC32 for verification                                             */
/* ------------------------------------------------------------------ */
static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ------------------------------------------------------------------ */
/* Test payloads                                                      */
/* ------------------------------------------------------------------ */
static const char json_508[] =
    "{\"id\":42,\"name\":\"Oliver Smith\",\"number\":1001,\"type\":\"order\","
    "\"status\":\"active\",\"data\":{\"value\":36.5,\"error\":false,"
    "\"state\":\"normal\",\"message\":\"all clear\"},\"items\":["
    "{\"id\":1,\"name\":\"Widget\",\"number\":100},"
    "{\"id\":2,\"name\":\"Grommet\",\"number\":200},"
    "{\"id\":3,\"name\":\"Sprocket\",\"number\":150}]}";

static const char prose_block[] =
    "Temp 36.5C at 14:32. Patient sync; stable. "
    "BP 120/78 normal. Telem active. SpO2 97%. ";

static void fill_pattern(uint8_t *buf, size_t len) {
    static const uint8_t pat[] = "AACCBBDDAACCBBDD1122334455667788";
    for (size_t i = 0; i < len; i++) {
        if ((i % 64) < 52) buf[i] = pat[i % 31];
        else buf[i] = (uint8_t)(i * 17u);
    }
}

static void fill_prose(uint8_t *buf, size_t len) {
    size_t plen = strlen(prose_block);
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)prose_block[i % plen];
}

static uint64_t now_us(void) { return time_us_64(); }

/* ------------------------------------------------------------------ */
/* Generic benchmark runner                                           */
/* ------------------------------------------------------------------ */
typedef int (*compress_fn)(const uint8_t *, size_t, uint8_t *, size_t, size_t *);
typedef int (*decompress_fn)(const uint8_t *, size_t, uint8_t *, size_t, size_t *);

static void run_bench(const char *codec, const char *payload,
                      const uint8_t *input, size_t input_len,
                      compress_fn enc_fn, decompress_fn dec_fn,
                      unsigned enc_ram, unsigned dec_ram, int runs) {
    size_t out_cap = input_len + 512;
    uint8_t *compressed = (uint8_t *)malloc(out_cap);
    uint8_t *restored   = (uint8_t *)malloc(input_len);
    if (!compressed || !restored) {
        printf("| %-12s | %-12s | MALLOC FAIL |\n", codec, payload);
        free(compressed); free(restored);
        return;
    }

    size_t comp_len = 0, rest_len = 0;

    /* Warm up */
    enc_fn(input, input_len, compressed, out_cap, &comp_len);

    /* Timed encode */
    uint64_t t0 = now_us();
    for (int i = 0; i < runs; i++) {
        comp_len = 0;
        enc_fn(input, input_len, compressed, out_cap, &comp_len);
    }
    uint64_t enc_us = (now_us() - t0) / runs;

    /* Timed decode */
    t0 = now_us();
    for (int i = 0; i < runs; i++) {
        rest_len = 0;
        dec_fn(compressed, comp_len, restored, input_len, &rest_len);
    }
    uint64_t dec_us = (now_us() - t0) / runs;

    /* Verify */
    uint32_t crc_in  = crc32(input, input_len);
    uint32_t crc_out = crc32(restored, rest_len);
    bool ok = (rest_len == input_len && crc_in == crc_out);

    float ratio   = comp_len > 0 ? (float)input_len / comp_len : 0;
    float enc_mbs = enc_us > 0 ? (float)input_len / enc_us : 0;
    float dec_mbs = dec_us > 0 ? (float)input_len / dec_us : 0;

    printf("| %-12s | %-12s | %5u | %5u | %5.2fx | %7lu | %7lu | %5.2f | %5.2f | %5.1fK | %5.1fK | %s |\n",
        codec, payload,
        (unsigned)input_len, (unsigned)comp_len, ratio,
        (unsigned long)enc_us, (unsigned long)dec_us,
        enc_mbs, dec_mbs,
        enc_ram / 1024.0f, dec_ram / 1024.0f,
        ok ? "PASS" : "FAIL");
    fflush(stdout);

    free(compressed);
    free(restored);
}

/* ------------------------------------------------------------------ */
/* Profile table: name, enc_fn, dec_fn, enc_ram, dec_ram              */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    compress_fn enc;
    decompress_fn dec;
    unsigned enc_ram;
    unsigned dec_ram;
} codec_t;

/* RAM estimates: hash_table + block + history + overhead */
#define MICRO_ENC_RAM  (2*256*1 + 192 + 64 + 64)
#define MICRO_DEC_RAM  (192 + 64 + 192 + 32)
#define MIN_ENC_RAM    (2*256*1 + 508 + 128 + 64)
#define MIN_DEC_RAM    (508 + 128 + 508 + 32)
#define BAL_ENC_RAM    (2*512*2 + 508 + 504 + 64)
#define BAL_DEC_RAM    (508 + 504 + 508 + 32)
#define Q3_ENC_RAM     (2*1024*2 + 508 + 1024 + 64)
#define Q3_DEC_RAM     (508 + 1024 + 508 + 32)
#define Q4_ENC_RAM     (2*2048*2 + 508 + 2048 + 64)
#define Q4_DEC_RAM     (508 + 2048 + 508 + 32)
#define HS_ENC_RAM_V   (2082)
#define HS_DEC_RAM_V   (2338)

static const codec_t codecs[] = {
    {"PC-Micro",    (compress_fn)pc_micro_compress_buffer, (decompress_fn)pc_micro_decompress_buffer, MICRO_ENC_RAM, MICRO_DEC_RAM},
    {"PC-Minimal",  (compress_fn)pc_min_compress_buffer,   (decompress_fn)pc_min_decompress_buffer,   MIN_ENC_RAM,   MIN_DEC_RAM},
    {"PC-Balanced", (compress_fn)pc_bal_compress_buffer,   (decompress_fn)pc_bal_decompress_buffer,   BAL_ENC_RAM,   BAL_DEC_RAM},
    {"PC-Q3",       (compress_fn)pc_q3_compress_buffer,    (decompress_fn)pc_q3_decompress_buffer,    Q3_ENC_RAM,    Q3_DEC_RAM},
    {"PC-Q4",       (compress_fn)pc_q4_compress_buffer,    (decompress_fn)pc_q4_decompress_buffer,    Q4_ENC_RAM,    Q4_DEC_RAM},
    {"heatshrink",  (compress_fn)hs_compress,              (decompress_fn)hs_decompress,              HS_ENC_RAM_V,  HS_DEC_RAM_V},
};
#define N_CODECS (sizeof(codecs)/sizeof(codecs[0]))

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */
int main() {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(500);

    printf("\n=== picocompress multi-profile benchmark — Pico 2W (RP2350, M33 @ 150 MHz) ===\n");
    printf("CPU: %lu MHz\n\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000));

    printf("| Codec        | Payload      |  Size | Comp  | Ratio |  Enc us |  Dec us | E MB/s | D MB/s | EncRAM | DecRAM | CRC  |\n");
    printf("|--------------|--------------|-------|-------|-------|---------|---------|--------|--------|--------|--------|------|\n");
    fflush(stdout);

    /* json-508 */
    {
        uint8_t buf[508];
        size_t jlen = strlen(json_508);
        memset(buf, ' ', sizeof(buf));
        memcpy(buf, json_508, jlen < 508 ? jlen : 508);
        for (unsigned c = 0; c < N_CODECS; c++)
            run_bench(codecs[c].name, "json-508", buf, 508,
                      codecs[c].enc, codecs[c].dec,
                      codecs[c].enc_ram, codecs[c].dec_ram,
                      c < 5 ? 10 : 10);
    }

    /* pattern-508 */
    {
        uint8_t buf[508];
        fill_pattern(buf, sizeof(buf));
        for (unsigned c = 0; c < N_CODECS; c++)
            run_bench(codecs[c].name, "pattern-508", buf, 508,
                      codecs[c].enc, codecs[c].dec,
                      codecs[c].enc_ram, codecs[c].dec_ram,
                      c < 5 ? 10 : 10);
    }

    /* prose-4K */
    {
        uint8_t *buf = (uint8_t *)malloc(4096);
        if (buf) {
            fill_prose(buf, 4096);
            for (unsigned c = 0; c < N_CODECS; c++)
                run_bench(codecs[c].name, "prose-4K", buf, 4096,
                          codecs[c].enc, codecs[c].dec,
                          codecs[c].enc_ram, codecs[c].dec_ram,
                          c < 5 ? 5 : 5);
            free(buf);
        }
    }

    /* prose-32K */
    {
        uint8_t *buf = (uint8_t *)malloc(32768);
        if (buf) {
            fill_prose(buf, 32768);
            for (unsigned c = 0; c < N_CODECS; c++)
                run_bench(codecs[c].name, "prose-32K", buf, 32768,
                          codecs[c].enc, codecs[c].dec,
                          codecs[c].enc_ram, codecs[c].dec_ram,
                          c < 5 ? 3 : 2);
            free(buf);
        }
    }

    /* random-508 */
    {
        uint8_t buf[508];
        srand(1337);
        for (int i = 0; i < 508; i++) buf[i] = (uint8_t)(rand() & 0xFF);
        for (unsigned c = 0; c < N_CODECS; c++)
            run_bench(codecs[c].name, "random-508", buf, 508,
                      codecs[c].enc, codecs[c].dec,
                      codecs[c].enc_ram, codecs[c].dec_ram,
                      c < 5 ? 10 : 10);
    }

    printf("\n=== benchmark complete ===\n");
    while (1) sleep_ms(10000);
    return 0;
}

