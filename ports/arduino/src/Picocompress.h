/*
 * Picocompress — Arduino-friendly C++ wrapper
 * SPDX-License-Identifier: MIT
 */
#ifndef PICOCOMPRESS_ARDUINO_H
#define PICOCOMPRESS_ARDUINO_H

#include <Arduino.h>

extern "C" {
#include "core/picocompress.h"
}

/**
 * Thin C++ wrapper around the picocompress buffer API.
 *
 * Usage:
 *   Picocompress pc;                         // default (block 508)
 *   Picocompress pc = Picocompress::Micro();  // block 252 — fits on Uno
 *
 *   size_t outLen;
 *   int rc = pc.compress(src, srcLen, dst, dstCap, &outLen);
 */
class Picocompress {
public:
    /**
     * Construct with a given block size.
     * The block size is informational — the actual codec block size is
     * set at compile time via PC_BLOCK_SIZE.  This field is stored so
     * profile presets can be distinguished at runtime if needed.
     */
    explicit Picocompress(uint16_t blockSize = PC_BLOCK_SIZE)
        : _blockSize(blockSize) {}

    /* ---------- Profile presets ------------------------------------ */

    /** Micro — smallest RAM footprint, good for AVR / Arduino Uno. */
    static Picocompress Micro()    { return Picocompress(252); }

    /** Balanced — default trade-off (same as default constructor). */
    static Picocompress Balanced() { return Picocompress(508); }

    /** Q4 — same block size, kept for API symmetry. */
    static Picocompress Q4()       { return Picocompress(508); }

    /* ---------- Buffer API ---------------------------------------- */

    /**
     * Compress @p inputLen bytes from @p input into @p output.
     * @param outputCap  capacity of the output buffer.
     * @param outputLen  receives the number of bytes written.
     * @return PC_OK (0) on success, negative pc_result on error.
     */
    int compress(const uint8_t *input, size_t inputLen,
                 uint8_t *output, size_t outputCap,
                 size_t *outputLen)
    {
        return static_cast<int>(
            pc_compress_buffer(input, inputLen, output, outputCap, outputLen));
    }

    /**
     * Decompress @p inputLen bytes from @p input into @p output.
     * @param outputCap  capacity of the output buffer.
     * @param outputLen  receives the number of bytes written.
     * @return PC_OK (0) on success, negative pc_result on error.
     */
    int decompress(const uint8_t *input, size_t inputLen,
                   uint8_t *output, size_t outputCap,
                   size_t *outputLen)
    {
        return static_cast<int>(
            pc_decompress_buffer(input, inputLen, output, outputCap, outputLen));
    }

    /* ---------- Helpers ------------------------------------------- */

    /** Upper bound on compressed size for a given input length. */
    static size_t compressBound(size_t inputLen)
    {
        return pc_compress_bound(inputLen);
    }

    /** Return the block size this instance was constructed with. */
    uint16_t blockSize() const { return _blockSize; }

private:
    uint16_t _blockSize;
};

#endif /* PICOCOMPRESS_ARDUINO_H */
