/*
 * picocompress benchmark for ESP32-CAM (Xtensa LX6, 520K SRAM)
 *
 * Build with Arduino IDE or PlatformIO:
 *   Board: AI Thinker ESP32-CAM
 *   Framework: Arduino
 *
 * Copy picocompress.h and picocompress.c into the same folder as this .ino,
 * or add them to your lib/ directory.
 *
 * Output goes to Serial at 115200 baud.
 */

/* Q4 profile — ESP32 has plenty of RAM */
#define PC_HASH_BITS       11u
#define PC_HASH_CHAIN_DEPTH 2u
#define PC_HISTORY_SIZE    2048u
#define PC_LAZY_STEPS       2u

#include "picocompress.h"
#include "picocompress.c"  /* single-file include for Arduino sketch */

/* ---- test payloads (generated to match benchmark_harness.py) ---- */

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

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void fill_pattern(uint8_t *buf, size_t len) {
    static const uint8_t pat[] = "AACCBBDDAACCBBDD1122334455667788";
    for (size_t i = 0; i < len; i++) {
        if ((i % 64) < 52)
            buf[i] = pat[i % 31];
        else
            buf[i] = (uint8_t)(i * 17u);
    }
}

static void fill_prose(uint8_t *buf, size_t len) {
    size_t plen = strlen(prose_block);
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)prose_block[i % plen];
}

typedef struct {
    const char *name;
    size_t size;
    void (*fill)(uint8_t *buf, size_t len);
    const char *literal;  /* if non-NULL, use this instead of fill */
} test_payload_t;

static void run_benchmark(const char *name, const uint8_t *input, size_t input_len) {
    uint8_t *compressed = (uint8_t *)malloc(input_len + 256);
    uint8_t *restored = (uint8_t *)malloc(input_len);
    if (!compressed || !restored) {
        Serial.printf("  %s: MALLOC FAILED\n", name);
        free(compressed); free(restored);
        return;
    }

    size_t comp_len = 0, rest_len = 0;

    /* Warm up */
    pc_compress_buffer(input, input_len, compressed, input_len + 256, &comp_len);

    /* Timed compress (average of 10 runs) */
    unsigned long t0 = micros();
    int runs = 10;
    for (int i = 0; i < runs; i++) {
        comp_len = 0;
        pc_compress_buffer(input, input_len, compressed, input_len + 256, &comp_len);
    }
    unsigned long enc_us = (micros() - t0) / runs;

    /* Timed decompress (average of 10 runs) */
    t0 = micros();
    for (int i = 0; i < runs; i++) {
        rest_len = 0;
        pc_decompress_buffer(compressed, comp_len, restored, input_len, &rest_len);
    }
    unsigned long dec_us = (micros() - t0) / runs;

    /* Verify */
    uint32_t crc_in = crc32(input, input_len);
    uint32_t crc_out = crc32(restored, rest_len);
    bool ok = (rest_len == input_len && crc_in == crc_out);

    float ratio = comp_len > 0 ? (float)input_len / comp_len : 0;
    float enc_mbs = enc_us > 0 ? (float)input_len / enc_us : 0;  /* bytes/us = MB/s */
    float dec_mbs = dec_us > 0 ? (float)input_len / dec_us : 0;

    Serial.printf("| %-16s | %6u | %6u | %5.2fx | %7lu | %7lu | %6.2f | %6.2f | %s |\n",
        name, (unsigned)input_len, (unsigned)comp_len, ratio,
        enc_us, dec_us, enc_mbs, dec_mbs,
        ok ? "PASS" : "FAIL");

    free(compressed);
    free(restored);
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("=== picocompress benchmark — ESP32-CAM ===");
    Serial.printf("CPU freq: %u MHz\n", getCpuFrequencyMhz());
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Profile: Q4 (b11 d2 h2048 lazy2)\n");
    Serial.println();

    Serial.println("| Payload          |   Size |   Comp | Ratio |  Enc us |  Dec us | Enc MB/s | Dec MB/s | CRC  |");
    Serial.println("|------------------|--------|--------|-------|---------|---------|----------|----------|------|");

    /* json-508 */
    {
        uint8_t buf[508];
        size_t jlen = strlen(json_508);
        memset(buf, ' ', sizeof(buf));
        memcpy(buf, json_508, jlen < 508 ? jlen : 508);
        run_benchmark("json-508", buf, sizeof(buf));
    }

    /* pattern-508 */
    {
        uint8_t buf[508];
        fill_pattern(buf, sizeof(buf));
        run_benchmark("pattern-508", buf, sizeof(buf));
    }

    /* prose-4K */
    {
        uint8_t *buf = (uint8_t *)malloc(4096);
        if (buf) {
            fill_prose(buf, 4096);
            run_benchmark("prose-4K", buf, 4096);
            free(buf);
        }
    }

    /* prose-32K */
    {
        uint8_t *buf = (uint8_t *)malloc(32768);
        if (buf) {
            fill_prose(buf, 32768);
            run_benchmark("prose-32K", buf, 32768);
            free(buf);
        }
    }

    /* random-508 */
    {
        uint8_t buf[508];
        srand(1337);
        for (int i = 0; i < 508; i++) buf[i] = (uint8_t)(rand() & 0xFF);
        run_benchmark("random-508", buf, sizeof(buf));
    }

    Serial.println();
    Serial.println("=== benchmark complete ===");
}

void loop() {
    delay(10000);
}
