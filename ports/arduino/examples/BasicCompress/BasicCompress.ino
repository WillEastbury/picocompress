/*
 * BasicCompress — compress and decompress a short string.
 *
 * Works on Arduino Uno (Micro profile), ESP32, Raspberry Pi Pico W,
 * and any other board supported by picocompress.
 */

#include <Picocompress.h>

static const char message[] =
    "Hello from Picocompress! This string is repeated. "
    "Hello from Picocompress! This string is repeated. "
    "Hello from Picocompress! This string is repeated.";

void setup() {
    Serial.begin(115200);
    while (!Serial) { /* wait for USB CDC on boards that need it */ }

    Serial.println(F("=== Picocompress BasicCompress ==="));
    Serial.println();

    Picocompress pc;  // default profile

    const size_t srcLen = strlen(message);
    const size_t compCap = Picocompress::compressBound(srcLen);

    uint8_t *compBuf = (uint8_t *)malloc(compCap);
    uint8_t *decBuf  = (uint8_t *)malloc(srcLen + 1);

    if (!compBuf || !decBuf) {
        Serial.println(F("ERROR: malloc failed"));
        free(compBuf);
        free(decBuf);
        return;
    }

    /* ---- Compress ------------------------------------------------ */
    size_t compLen = 0;
    unsigned long t0 = micros();
    int rc = pc.compress((const uint8_t *)message, srcLen,
                         compBuf, compCap, &compLen);
    unsigned long tCompress = micros() - t0;

    if (rc != 0) {
        Serial.print(F("Compress error: "));
        Serial.println(rc);
        free(compBuf);
        free(decBuf);
        return;
    }

    Serial.print(F("Original size : "));
    Serial.println(srcLen);
    Serial.print(F("Compressed size: "));
    Serial.println(compLen);
    Serial.print(F("Ratio          : "));
    Serial.print((float)compLen / srcLen * 100.0f, 1);
    Serial.println(F("%"));
    Serial.print(F("Compress time  : "));
    Serial.print(tCompress);
    Serial.println(F(" us"));

    /* ---- Decompress ---------------------------------------------- */
    size_t decLen = 0;
    t0 = micros();
    rc = pc.decompress(compBuf, compLen, decBuf, srcLen, &decLen);
    unsigned long tDecompress = micros() - t0;

    if (rc != 0) {
        Serial.print(F("Decompress error: "));
        Serial.println(rc);
    } else {
        decBuf[decLen] = '\0';
        Serial.print(F("Decompress time: "));
        Serial.print(tDecompress);
        Serial.println(F(" us"));
        Serial.print(F("Round-trip OK  : "));
        Serial.println(memcmp(message, decBuf, srcLen) == 0 ? F("YES") : F("NO"));
    }

    free(compBuf);
    free(decBuf);

    Serial.println();
    Serial.println(F("Done."));
}

void loop() {
    // nothing to do
}
