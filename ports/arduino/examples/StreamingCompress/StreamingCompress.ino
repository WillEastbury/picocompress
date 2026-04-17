/*
 * StreamingCompress — use the streaming encoder API to compress data
 * in small chunks and write compressed bytes to Serial.
 *
 * Demonstrates pc_encoder_sink / pc_encoder_finish with a callback
 * that prints hex-encoded compressed output.
 *
 * Works on Arduino Uno, ESP32, Raspberry Pi Pico W, etc.
 */

#include <Picocompress.h>

/* ---- Writer callback: hex-dump compressed bytes to Serial -------- */

static size_t s_totalOut = 0;

static int writeHex(void *user, const uint8_t *data, size_t len) {
    (void)user;
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print('0');
        Serial.print(data[i], HEX);
    }
    s_totalOut += len;
    return 0;
}

/* ---- Sample data ------------------------------------------------- */

static const char chunk1[] = "The quick brown fox jumps over the lazy dog. ";
static const char chunk2[] = "The quick brown fox jumps over the lazy dog. ";
static const char chunk3[] = "Pack my box with five dozen liquor jugs.";

void setup() {
    Serial.begin(115200);
    while (!Serial) { /* wait for USB CDC */ }

    Serial.println(F("=== Picocompress StreamingCompress ==="));
    Serial.println();

    pc_encoder enc;
    pc_encoder_init(&enc);

    s_totalOut = 0;
    size_t totalIn = 0;

    unsigned long t0 = micros();

    /* Feed data in three separate calls — the encoder buffers
     * internally and emits compressed blocks as they fill. */
    Serial.print(F("Compressed hex: "));

    pc_result rc;
    rc = pc_encoder_sink(&enc, (const uint8_t *)chunk1,
                         strlen(chunk1), writeHex, NULL);
    if (rc != PC_OK) { Serial.print(F("\nSink error: ")); Serial.println(rc); return; }
    totalIn += strlen(chunk1);

    rc = pc_encoder_sink(&enc, (const uint8_t *)chunk2,
                         strlen(chunk2), writeHex, NULL);
    if (rc != PC_OK) { Serial.print(F("\nSink error: ")); Serial.println(rc); return; }
    totalIn += strlen(chunk2);

    rc = pc_encoder_sink(&enc, (const uint8_t *)chunk3,
                         strlen(chunk3), writeHex, NULL);
    if (rc != PC_OK) { Serial.print(F("\nSink error: ")); Serial.println(rc); return; }
    totalIn += strlen(chunk3);

    /* Flush the final (partial) block. */
    rc = pc_encoder_finish(&enc, writeHex, NULL);
    if (rc != PC_OK) { Serial.print(F("\nFinish error: ")); Serial.println(rc); return; }

    unsigned long elapsed = micros() - t0;

    Serial.println();
    Serial.println();
    Serial.print(F("Input bytes    : "));
    Serial.println(totalIn);
    Serial.print(F("Output bytes   : "));
    Serial.println(s_totalOut);
    Serial.print(F("Ratio          : "));
    Serial.print((float)s_totalOut / totalIn * 100.0f, 1);
    Serial.println(F("%"));
    Serial.print(F("Encode time    : "));
    Serial.print(elapsed);
    Serial.println(F(" us"));
    Serial.println();
    Serial.println(F("Done."));
}

void loop() {
    // nothing to do
}
